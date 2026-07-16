/**
 * @file vision_task.c
 * @brief Core-1 vision task: capture → face-detect → publish via queue.
 *
 * Pinned to Core 1 so that ESP-DL inference never starves the audio
 * localisation loop running on Core 0.  An overwrite-mode queue provides
 * lock-free, always-fresh communication between the two cores.
 */

#include "vision_task.h"
#include "drv_ov2640.h"
#include "face_detector.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "vision_task";

/* ---- Task configuration ---- */
#define VISION_TASK_STACK      16384
#define VISION_TASK_PRIORITY   4
#define VISION_TASK_CORE       1
#define VISION_QUEUE_LEN       1   /* Overwrite mode: always read latest */

/* ---- Timing ---- */
#define FPS_LOG_INTERVAL_US    (5ULL * 1000ULL * 1000ULL)   /* 5 s   */
#define DELAY_FACE_FOUND_MS    100   /* ~10 fps when tracking */
#define DELAY_IDLE_MS          500   /* ~2 fps when idle      */

static QueueHandle_t s_vision_queue = NULL;

/* ------------------------------------------------------------------ */
/*  Vision task                                                       */
/* ------------------------------------------------------------------ */

static void vision_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "started on core %d", VISION_TASK_CORE);
    ESP_LOGI(TAG, "free heap: %lu bytes, free PSRAM: %lu bytes",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Load the ESP-DL face-detection model on this core. */
    esp_err_t ret = face_detector_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "face_detector_init failed: %s — aborting task",
                 esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    uint32_t frame_count = 0;
    uint32_t face_count  = 0;
    int64_t  fps_t0      = esp_timer_get_time();

    camera_frame_t frame;
    face_result_t  result;

    while (1) {
        /* 1. Capture a frame from the OV2640 (RGB565). */
        ret = drv_ov2640_capture(&frame);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "frame capture failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(DELAY_IDLE_MS));
            continue;
        }

        /* 2. Run ESP-DL face detection. */
        ret = face_detector_detect(&frame, &result);

        /* 3. Return the frame buffer to the camera driver ASAP. */
        drv_ov2640_return_frame(&frame);

        /* 4. Build the message (default: no face). */
        vision_msg_t msg = {
            .face_found   = false,
            .face_cx      = 0,
            .face_cy      = 0,
            .face_w       = 0,
            .face_h       = 0,
            .inference_ms = 0,
        };

        bool face_found = false;
        if (ret == ESP_OK && result.count > 0) {
            const face_box_t *largest = face_detector_pick_largest(&result);
            if (largest != NULL) {
                face_found        = true;
                msg.face_found    = true;
                msg.face_cx       = (int16_t)((int)largest->x + (int)(largest->width / 2));
                msg.face_cy       = (int16_t)((int)largest->y + (int)(largest->height / 2));
                msg.face_w        = largest->width;
                msg.face_h        = largest->height;
                msg.inference_ms  = result.inference_ms;
            }
        }

        /* 5. Publish the latest result (overwrite any stale entry). */
        xQueueOverwrite(s_vision_queue, &msg);

        /* 6. Statistics — log every 5 seconds. */
        frame_count++;
        if (face_found) {
            face_count++;
        }

        int64_t now = esp_timer_get_time();
        if (now - fps_t0 >= FPS_LOG_INTERVAL_US) {
            float elapsed_s = (float)(now - fps_t0) / 1000000.0f;
            float fps       = (float)frame_count / elapsed_s;
            ESP_LOGI(TAG, "fps=%.1f  frames=%lu  faces=%lu  inf=%lums",
                     fps, (unsigned long)frame_count, (unsigned long)face_count,
                     (unsigned long)msg.inference_ms);
            frame_count = 0;
            face_count  = 0;
            fps_t0      = now;
        }

        /* 7. Adaptive frame rate: faster when a face is present. */
        vTaskDelay(pdMS_TO_TICKS(face_found ? DELAY_FACE_FOUND_MS : DELAY_IDLE_MS));
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

esp_err_t vision_task_init(void)
{
    if (s_vision_queue != NULL) {
        ESP_LOGW(TAG, "vision task already initialised");
        return ESP_OK;
    }

    /* Overwrite-mode queue: consumers always read the freshest result. */
    s_vision_queue = xQueueCreate(VISION_QUEUE_LEN, sizeof(vision_msg_t));
    if (s_vision_queue == NULL) {
        ESP_LOGE(TAG, "failed to create vision queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        vision_task,
        "vision",
        VISION_TASK_STACK,
        NULL,
        VISION_TASK_PRIORITY,
        NULL,
        VISION_TASK_CORE);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create vision task");
        vQueueDelete(s_vision_queue);
        s_vision_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "vision task created (core %d, stack %d, prio %d)",
             VISION_TASK_CORE, VISION_TASK_STACK, VISION_TASK_PRIORITY);
    return ESP_OK;
}

bool vision_get_latest(vision_msg_t *msg)
{
    if (msg == NULL || s_vision_queue == NULL) {
        return false;
    }
    /* Consuming read: each vision frame is processed exactly once by the
       state machine, preventing PID over-sampling at the 200 Hz audio
       loop rate.  When the queue is empty, returns false immediately. */
    return (xQueueReceive(s_vision_queue, msg, 0) == pdTRUE);
}
