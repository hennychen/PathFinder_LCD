/**
 * @file face_tracker.c
 * @brief 人脸检测流水线 — 双任务并行架构（对齐 face_tracker_pipeline）
 *
 * =========================================================================
 *  流水线架构 (Pipeline Architecture):
 *
 *  ┌──────────────┐  frame_queue(2)  ┌────────────────┐
 *  │  cam_task    │ ───────────────▶ │  detect_task   │
 *  │   (CPU 0)    │  camera_fb_t*    │   (CPU 1)      │
 *  │              │                  │                │
 *  │ lock→fb_get  │                  │ ESP-DL 推理     │
 *  │ →unlock→queue│                  │ →fb_return     │
 *  │              │                  │ →PID→coordinator│
 *  └──────────────┘                  └────────────────┘
 *
 *  关键优化（vs 串行架构）：
 *    1. 采集和推理并行：cam_task 在 detect_task 推理时已采集下一帧
 *    2. camera_fb_lock 仅持有 μs 级（fb_get 即解锁），不阻塞 HTTP 流
 *    3. fb_count=2 双缓冲，摄像头永不等空闲缓冲
 *    4. 帧队列深度=2，匹配双缓冲 ping-pong
 *
 *  目标帧率: 14–18 FPS（与 face_tracker_pipeline 对齐）
 * =========================================================================
 */

#include "face_tracker.h"
#include "face_detect_dl.h"
#include "tracking_coordinator.h"
#include "camera_http_server.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <math.h>
#include <string.h>

#define TAG "FaceTracker"

/* ========================================================================
 *  帧参数
 * ======================================================================== */
#define FRAME_W              320
#define FRAME_H              240
#define FRAME_CX             (FRAME_W / 2)
#define FRAME_CY             (FRAME_H / 2)

/* ========================================================================
 *  PID 控制器参数
 * ======================================================================== */
#define PID_PAN_KP           0.20f
#define PID_PAN_KI           0.005f
#define PID_PAN_KD           0.08f
#define PID_PAN_DEADBAND_PX  10     /* Pan 死区 */

#define PID_TILT_KP          0.20f  /* 与 Pan 一致（原 0.15 太小） */
#define PID_TILT_KI          0.005f
#define PID_TILT_KD          0.08f
#define PID_TILT_DEADBAND_PX 5      /* Tilt 死区减半（人脸垂直偏移通常较小） */
#define PID_OUTPUT_LIMIT     30.0f
#define PID_INTEGRAL_LIMIT   100.0f

/* ========================================================================
 *  流水线任务参数
 * ======================================================================== */
#define CAM_TASK_STACK       8192
#define CAM_TASK_PRIO        4
#define CAM_TASK_CORE        0        /* 采集在 Core 0（DVP DMA 绑定） */

#define DETECT_TASK_STACK    16384
#define DETECT_TASK_PRIO     3
#define DETECT_TASK_CORE     1        /* 推理在 Core 1（独占） */

#define FRAME_QUEUE_LEN      2        /* 匹配 fb_count=2 双缓冲 */

#define EMA_ALPHA            0.5f

/* ========================================================================
 *  状态变量
 * ======================================================================== */
static bool s_running  = false;
static bool s_detected = false;

/* 流水线任务句柄 */
static TaskHandle_t s_cam_task    = NULL;
static TaskHandle_t s_detect_task = NULL;
static QueueHandle_t s_frame_queue = NULL;

/* EMA 平滑 */
static float s_ema_cx = (float)FRAME_CX;
static float s_ema_cy = (float)FRAME_CY;
static bool  s_ema_init = false;

/* 最新人脸信息 */
static face_info_t s_last_face = {0};

/* PID 控制器实例 */
static face_pid_t s_pid_pan  = {0};
static face_pid_t s_pid_tilt = {0};

/* FPS 统计 */
static int64_t s_last_fps_time = 0;
static int s_infer_count = 0;
static float s_avg_infer_ms = 0.0f;

/* ========================================================================
 *  PID 控制器实现
 * ======================================================================== */

float face_pid_compute(face_pid_t *pid, float setpoint, float measured, int deadband)
{
    if (!pid) return 0.0f;

    float error = setpoint - measured;

    if (fabsf(error) < deadband) {
        pid->prev_error = error;
        return 0.0f;
    }

    pid->integral += error;
    if (pid->integral > PID_INTEGRAL_LIMIT)  pid->integral = PID_INTEGRAL_LIMIT;
    if (pid->integral < -PID_INTEGRAL_LIMIT) pid->integral = -PID_INTEGRAL_LIMIT;

    float derivative = error - pid->prev_error;
    pid->prev_error = error;

    float output = (pid->Kp * error)
                 + (pid->Ki * pid->integral)
                 + (pid->Kd * derivative);

    if (output > PID_OUTPUT_LIMIT)  output = PID_OUTPUT_LIMIT;
    if (output < -PID_OUTPUT_LIMIT) output = -PID_OUTPUT_LIMIT;

    return output;
}

/* ========================================================================
 *  Stage 1: cam_task — 帧采集 (CPU 0)
 *
 *  持有 camera_fb_lock 仅 μs 级（fb_get 调用），解锁后立即入队。
 *  采集与推理完全并行：detect_task 推理当前帧时 cam_task 已在采集下一帧。
 * ======================================================================== */

static void cam_task_fn(void *arg)
{
    ESP_LOGI(TAG, "Cam task started on Core %d", xPortGetCoreID());

    while (s_running) {
        /* ── 获取摄像头互斥锁（与 HTTP MJPEG 流共享） ── */
        if (!camera_fb_lock()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* ── 获取一帧 ── */
        camera_fb_t *fb = esp_camera_fb_get();

        /* ── 立即释放互斥锁 ──（关键！不等推理） */
        camera_fb_unlock();

        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* ── 帧有效性检查 ── */
        if (fb->format != PIXFORMAT_RGB565 ||
            fb->width != FRAME_W || fb->height != FRAME_H) {
            esp_camera_fb_return(fb);
            continue;
        }

        /* ── 发送到 detect_task（非阻塞） ──
         * 队列满 → detect_task 还在推理上一帧，丢弃当前帧 */
        if (xQueueSend(s_frame_queue, &fb, 0) != pdTRUE) {
            esp_camera_fb_return(fb);
        }

        /* 短暂 yield，让 HTTP server 有机会获取锁 */
        taskYIELD();
    }

    /* 清理：排空队列并归还所有帧 */
    camera_fb_t *fb;
    while (xQueueReceive(s_frame_queue, &fb, 0) == pdTRUE) {
        esp_camera_fb_return(fb);
    }

    s_cam_task = NULL;
    ESP_LOGI(TAG, "Cam task stopped");
    vTaskDelete(NULL);
}

/* ========================================================================
 *  Stage 2: detect_task — ESP-DL 推理 + PID 控制 (CPU 1)
 * ======================================================================== */

static void detect_task_fn(void *arg)
{
    ESP_LOGI(TAG, "Detect task started on Core %d", xPortGetCoreID());

    /* 初始化 PID 控制器 */
    s_pid_pan  = (face_pid_t){ .Kp = PID_PAN_KP,  .Ki = PID_PAN_KI,  .Kd = PID_PAN_KD,
                                .integral = 0.0f, .prev_error = 0.0f };
    s_pid_tilt = (face_pid_t){ .Kp = PID_TILT_KP, .Ki = PID_TILT_KI, .Kd = PID_TILT_KD,
                                .integral = 0.0f, .prev_error = 0.0f };

    int lost_count = 0;

    s_last_fps_time = esp_timer_get_time();
    s_infer_count = 0;
    s_avg_infer_ms = 0.0f;

    while (s_running) {
        /* ── 阻塞等待 cam_task 发来的帧 ── */
        camera_fb_t *fb = NULL;
        if (xQueueReceive(s_frame_queue, &fb, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        if (!fb) continue;

        /* ── ESP-DL 神经网络推理 ── */
        int64_t t0 = esp_timer_get_time();
        bool found = face_detect_dl_detect(fb->buf, fb->width, fb->height);
        int64_t infer_ms = (esp_timer_get_time() - t0) / 1000;

        /* ── 立即归还帧缓冲（关键！让 cam_task 能继续采集） ── */
        esp_camera_fb_return(fb);

        s_detected = found;

        face_detect_result_t dl_result;
        face_detect_dl_get_result(&dl_result);

        /* ── PID 控制 ── */
        if (found && dl_result.detected) {
            int raw_cx = dl_result.cx;
            int raw_cy = dl_result.cy;
            lost_count = 0;

            /* EMA 时间域平滑 */
            if (!s_ema_init) {
                s_ema_cx = (float)raw_cx;
                s_ema_cy = (float)raw_cy;
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

            float err_x = (float)(cx - FRAME_CX);
            float err_y = (float)(cy - FRAME_CY);

            float pan_delta_f  = face_pid_compute(&s_pid_pan,  0.0f, err_x,  PID_PAN_DEADBAND_PX);
            float tilt_delta_f = face_pid_compute(&s_pid_tilt, 0.0f, err_y, PID_TILT_DEADBAND_PX);

            /* lroundf 替代 (int) 截断，保留亚度级精度 */
            int pan_delta  = (int)lroundf(pan_delta_f);
            int tilt_delta = (int)lroundf(tilt_delta_f);

            ESP_LOGD(TAG, "PID: cx=%d cy=%d err_x=%.0f err_y=%.0f | pan_f=%.1f→%d tilt_f=%.1f→%d",
                     cx, cy, err_x, err_y, pan_delta_f, pan_delta, tilt_delta_f, tilt_delta);

            if (pan_delta != 0 || tilt_delta != 0) {
                tracking_on_face_update(pan_delta, tilt_delta);
            }
        } else {
            s_last_face.detected = false;
            lost_count++;
            if (lost_count == 8) {
                s_ema_init = false;
                s_pid_pan.integral  = 0.0f;
                s_pid_tilt.integral = 0.0f;
                tracking_face_lost();
            }
        }

        /* ── FPS 统计 ── */
        s_infer_count++;
        s_avg_infer_ms = s_avg_infer_ms * 0.9f + (float)infer_ms * 0.1f;
        int64_t now = esp_timer_get_time();
        if (now - s_last_fps_time >= 1000000) {
            float fps = (float)s_infer_count * 1000000.0f / (float)(now - s_last_fps_time);
            ESP_LOGI(TAG, "Inference FPS: %.1f | avg infer: %.1f ms | face: %s",
                     fps, s_avg_infer_ms,
                     s_detected ? "YES" : "NO");
            s_infer_count = 0;
            s_last_fps_time = now;
        }
    }

    s_detect_task = NULL;
    s_detected = false;
    s_ema_init = false;
    s_last_face.detected = false;
    ESP_LOGI(TAG, "Detect task stopped");
    vTaskDelete(NULL);
}

/* ========================================================================
 *  公共 API
 * ======================================================================== */

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

    s_running  = true;
    s_detected = false;
    s_ema_init = false;

    s_pid_pan.integral  = 0.0f;
    s_pid_pan.prev_error = 0.0f;
    s_pid_tilt.integral = 0.0f;
    s_pid_tilt.prev_error = 0.0f;

    tracking_set_mode(TRACK_MODE_FACE);

    /* 创建帧队列 */
    s_frame_queue = xQueueCreate(FRAME_QUEUE_LEN, sizeof(camera_fb_t *));
    if (!s_frame_queue) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        s_running = false;
        return;
    }

    /* Stage 1: cam_task (Core 0) */
    BaseType_t r1 = xTaskCreatePinnedToCore(cam_task_fn, "cam_ft",
                                            CAM_TASK_STACK, NULL, CAM_TASK_PRIO,
                                            &s_cam_task, CAM_TASK_CORE);
    if (r1 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create cam_task");
        vQueueDelete(s_frame_queue);
        s_frame_queue = NULL;
        s_running = false;
        return;
    }

    /* Stage 2: detect_task (Core 1) */
    BaseType_t r2 = xTaskCreatePinnedToCore(detect_task_fn, "det_ft",
                                            DETECT_TASK_STACK, NULL, DETECT_TASK_PRIO,
                                            &s_detect_task, DETECT_TASK_CORE);
    if (r2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create detect_task");
        s_running = false;
        /* cam_task 会因 s_running=false 而退出 */
        vTaskDelay(pdMS_TO_TICKS(100));
        vQueueDelete(s_frame_queue);
        s_frame_queue = NULL;
        return;
    }

    ESP_LOGI(TAG, "Pipeline started: cam_task(C%d) → queue(%d) → detect_task(C%d)",
             CAM_TASK_CORE, FRAME_QUEUE_LEN, DETECT_TASK_CORE);
}

void face_tracker_stop(void)
{
    if (!s_running) return;

    s_running = false;
    ESP_LOGI(TAG, "Face tracker stop requested");

    /* 等待两个任务结束 */
    int wait = 0;
    while ((s_cam_task || s_detect_task) && wait < 2000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait += 50;
    }

    if (s_frame_queue) {
        vQueueDelete(s_frame_queue);
        s_frame_queue = NULL;
    }

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
