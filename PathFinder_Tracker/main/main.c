#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tracker_config.h"
#include "drv_es7210.h"
#include "drv_ws2812.h"
#include "drv_servo.h"
#include "drv_uart_comm.h"
#include "drv_ov2640.h"
#include "sound_localizer.h"
#include "tracker_state_machine.h"
#include "vision_task.h"

static const char *TAG = "tracker_main";
static tracker_ctx_t s_tracker_ctx;

void app_main(void)
{
    printf("[%s] PathFinder_Tracker Phase 3 starting\n", TAG);

    /* WS2812 LED ring – initialise before ES7210 so visual feedback is
       available as soon as audio processing begins. */
    esp_err_t ret = drv_ws2812_init();
    if (ret != ESP_OK) {
        printf("[%s] WS2812 init FAILED: %s\n", TAG, esp_err_to_name(ret));
        /* Non-fatal: continue without LED ring. */
    }

    /* MG90S Pan/Tilt servos – initialise before ES7210 so the gimbal is
       centred before tracking starts. */
    ret = drv_servo_init();
    if (ret != ESP_OK) {
        printf("[%s] Servo init FAILED: %s\n", TAG, esp_err_to_name(ret));
        /* Non-fatal: continue without servo control. */
    }

    /* UART inter-board communication – initialise after servos but before
       ES7210 so angle data can be forwarded to Board A from the first frame. */
    ret = drv_uart_init();
    if (ret != ESP_OK) {
        printf("[%s] UART comm init FAILED: %s\n", TAG, esp_err_to_name(ret));
        /* Non-fatal: continue without inter-board communication. */
    }

    /* OV2640 camera — initialise before ES7210 */
    ret = drv_ov2640_init();
    if (ret != ESP_OK) {
        printf("[%s] Camera init FAILED (non-fatal): %s\n", TAG, esp_err_to_name(ret));
    }

    /* Launch the Core-1 vision task (face detection + queue publisher).
       Runs independently of the audio loop on Core 0. */
    ret = vision_task_init();
    if (ret != ESP_OK) {
        printf("[%s] Vision task init FAILED (non-fatal): %s\n", TAG, esp_err_to_name(ret));
    }

    ret = drv_es7210_init();
    if (ret != ESP_OK) {
        printf("[%s] ES7210 init FAILED: %s\n", TAG, esp_err_to_name(ret));
        return;
    }

    sound_localizer_init(ES7210_SAMPLE_RATE);
    sound_localizer_set_threshold(-2.7f);

    /* Initialise the closed-loop state machine. */
    tracker_sm_init(&s_tracker_ctx);

    /* Main processing loop.
     *
     * Audio runs at ~48 kHz with 256-sample frames (~5 ms per DMA read).
     * Vision messages arrive at ~10 Hz from Core 1.  We poll the
     * overwrite queue each audio frame — xQueuePeek is non-blocking so
     * there is zero cost when no new message is available.
     */
    static float mic_data[4][256];
    vision_msg_t vmsg;

    while (1) {
        if (drv_es7210_read(mic_data) == ESP_OK) {
            localization_result_t result = sound_localizer_compute(mic_data);
            tracker_sm_step(&s_tracker_ctx, result.angle, result.valid);
        }

        /* Check for new vision data (non-blocking).  We peek every ~5 ms
           but the underlying queue is overwrite-mode so we always see
           the latest result if one exists. */
        if (vision_get_latest(&vmsg)) {
            tracker_sm_step_vision(&s_tracker_ctx, vmsg.face_found,
                                   vmsg.face_cx, vmsg.face_cy,
                                   vmsg.face_w, vmsg.face_h);
        }

        taskYIELD();  /* i2s_channel_read already blocks ~5ms; no extra delay needed */
    }
}
