/**
 * @file face_tracker.c
 * @brief 人脸检测与追踪模块（ESP-DL 神经网络版）
 *
 * 算法流程（每帧）：
 *   1. esp_camera_fb_get() 获取 RGB565 QVGA 帧 (320x240)
 *   2. face_detect_dl_detect() 调用 MSRMNP_S8_V1 神经网络做人脸检测
 *   3. 时间域 EMA 平滑 → 降低单帧噪声
 *   4. 比例控制器：偏移量 × 增益 → Pan/Tilt 角度增量
 *   5. 通过 tracking_on_face_update() 驱动舵机平滑追踪
 */

#include "face_tracker.h"
#include "face_detect_dl.h"
#include "tracking_coordinator.h"
#include "camera_http_server.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define TAG "FaceTracker"

#define FRAME_W          320
#define FRAME_H          240
#define FRAME_CX         (FRAME_W / 2)
#define FRAME_CY         (FRAME_H / 2)

#define GAIN_PAN         0.10f
#define GAIN_TILT        0.08f
#define DEADBAND         20
#define MAX_DELTA_PER_FRAME  3
#define TASK_PERIOD_MS   150
#define TASK_STACK       8192
#define TASK_PRIO        3

#define EMA_ALPHA        0.5f

static bool s_running  = false;
static bool s_detected = false;
static TaskHandle_t s_task = NULL;

static float s_ema_cx = (float)FRAME_CX;
static float s_ema_cy = (float)FRAME_CY;
static bool  s_ema_init = false;

static face_info_t s_last_face = {0};

static void face_tracker_task(void *arg)
{
    ESP_LOGI(TAG, "Face tracker task started (ESP-DL MSRMNP, %dx%d, %dHz)",
             FRAME_W, FRAME_H, 1000 / TASK_PERIOD_MS);

    int lost_count = 0;

    while (s_running) {
        if (!camera_fb_lock()) {
            vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
            continue;
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            camera_fb_unlock();
            vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
            continue;
        }

        if (fb->format != PIXFORMAT_RGB565 ||
            fb->width != FRAME_W || fb->height != FRAME_H) {
            esp_camera_fb_return(fb);
            camera_fb_unlock();
            vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
            continue;
        }

        bool found = face_detect_dl_detect(fb->buf, fb->width, fb->height);

        esp_camera_fb_return(fb);
        camera_fb_unlock();

        s_detected = found;

        face_detect_result_t dl_result;
        face_detect_dl_get_result(&dl_result);

        if (found && dl_result.detected) {
            int raw_cx = dl_result.cx;
            int raw_cy = dl_result.cy;
            lost_count = 0;

            if (!s_ema_init) {
                s_ema_cx   = (float)raw_cx;
                s_ema_cy   = (float)raw_cy;
                s_ema_init = true;
            } else {
                s_ema_cx = EMA_ALPHA * raw_cx + (1.0f - EMA_ALPHA) * s_ema_cx;
                s_ema_cy = EMA_ALPHA * raw_cy + (1.0f - EMA_ALPHA) * s_ema_cy;
            }

            int cx = (int)(s_ema_cx + 0.5f);
            int cy = (int)(s_ema_cy + 0.5f);

            s_last_face.detected = true;
            s_last_face.cx = cx;
            s_last_face.cy = cy;
            s_last_face.w  = dl_result.w;
            s_last_face.h  = dl_result.h;
            s_last_face.score = dl_result.score;

            int dx = cx - FRAME_CX;
            int dy = cy - FRAME_CY;

            if (abs(dx) < DEADBAND && abs(dy) < DEADBAND) {
                goto next_frame;
            }

            int pan_delta = 0, tilt_delta = 0;
            if (abs(dx) >= DEADBAND) {
                pan_delta = (int)(dx * GAIN_PAN);
                if (pan_delta > MAX_DELTA_PER_FRAME)  pan_delta = MAX_DELTA_PER_FRAME;
                if (pan_delta < -MAX_DELTA_PER_FRAME) pan_delta = -MAX_DELTA_PER_FRAME;
            }
            if (abs(dy) >= DEADBAND) {
                tilt_delta = -(int)(dy * GAIN_TILT);
                if (tilt_delta > MAX_DELTA_PER_FRAME)  tilt_delta = MAX_DELTA_PER_FRAME;
                if (tilt_delta < -MAX_DELTA_PER_FRAME) tilt_delta = -MAX_DELTA_PER_FRAME;
            }

            tracking_on_face_update(pan_delta, tilt_delta);

            ESP_LOGD(TAG, "Face score=%.2f at (%d,%d) EMA(%d,%d) dx=%d dy=%d panΔ=%d tiltΔ=%d",
                     dl_result.score, raw_cx, raw_cy, cx, cy, dx, dy, pan_delta, tilt_delta);
        } else {
            s_last_face.detected = false;
            s_last_face.cx = 0;
            s_last_face.cy = 0;
            s_last_face.w  = 0;
            s_last_face.h  = 0;
            s_last_face.score = 0.0f;

            lost_count++;
            if (lost_count == 8) {
                s_ema_init = false;
                tracking_face_lost();
                ESP_LOGD(TAG, "Face lost → returning to center");
            }
        }

    next_frame:
        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
    }

    s_task = NULL;
    s_detected = false;
    s_ema_init = false;
    s_last_face.detected = false;
    ESP_LOGI(TAG, "Face tracker task stopped");
    vTaskDelete(NULL);
}

void face_tracker_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return;
    }

    if (!face_detect_dl_is_loaded()) {
        ESP_LOGI(TAG, "Pre-loading ESP-DL model...");
        if (!face_detect_dl_init()) {
            ESP_LOGE(TAG, "Failed to load ESP-DL face detection model");
            return;
        }
    }

    s_running = true;
    s_detected = false;

    tracking_set_mode(TRACK_MODE_FACE);

    BaseType_t ret = xTaskCreate(face_tracker_task, "face_track",
                                 TASK_STACK, NULL, TASK_PRIO, &s_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "Face tracker started (mode=FACE, ESP-DL MSRMNP)");
}

void face_tracker_stop(void)
{
    if (!s_running) {
        return;
    }

    s_running = false;
    ESP_LOGI(TAG, "Face tracker stop requested");

    tracking_set_mode(TRACK_MODE_AUTO);
}

bool face_tracker_is_running(void)
{
    return s_running;
}

bool face_tracker_detected(void)
{
    return s_detected;
}

void face_tracker_get_info(face_info_t *info)
{
    if (info) {
        *info = s_last_face;
    }
}
