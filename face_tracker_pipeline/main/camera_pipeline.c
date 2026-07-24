/**
 * @file camera_pipeline.c
 * @brief 摄像头初始化与帧采集任务 (Pipeline Stage 1, CPU 0)
 *
 * =========================================================================
 *  职责：
 *    1. camera_init() — 配置 OV2640 DVP 接口，QVGA 分辨率，PSRAM 帧缓存
 *    2. cam_task()    — 死循环采集帧，通过 xQueueSend 发送给 detect_task
 *
 *  数据流：
 *    OV2640 → esp_camera_fb_get() → g_frame_queue → detect_task
 *
 *  帧生命周期管理：
 *    cam_task 获取帧 → detect_task 消费并归还 (esp_camera_fb_return)
 *    双帧缓存 (fb_count=2) 实现 ping-pong，采集与推理并行
 * =========================================================================
 */

#include "face_tracker.h"

/* ── ESP-DL 辅助头文件 ── */
#include "esp_psram.h"
#include "esp_system.h"

#define TAG "CamPipeline"

/* 全局队列句柄定义 (在 face_tracker.h 中 extern 声明) */
QueueHandle_t g_frame_queue   = NULL;
QueueHandle_t g_control_queue = NULL;

/* ======================================================================== */
/*                          camera_init                                      */
/* ======================================================================== */

void camera_init(void)
{
    ESP_LOGI(TAG, "Initializing OV2640 camera (QVGA %dx%d)...", FRAME_WIDTH, FRAME_HEIGHT);

    /* ── 1. 组装 camera_config_t ── */
    camera_config_t config = {
        /* DVP 8 位并行数据线 */
        .pin_d0       = CAMERA_PIN_D0,
        .pin_d1       = CAMERA_PIN_D1,
        .pin_d2       = CAMERA_PIN_D2,
        .pin_d3       = CAMERA_PIN_D3,
        .pin_d4       = CAMERA_PIN_D4,
        .pin_d5       = CAMERA_PIN_D5,
        .pin_d6       = CAMERA_PIN_D6,
        .pin_d7       = CAMERA_PIN_D7,

        /* 时钟与同步信号 */
        .pin_xclk     = CAMERA_PIN_XCLK,
        .pin_pclk     = CAMERA_PIN_PCLK,
        .pin_vsync    = CAMERA_PIN_VSYNC,
        .pin_href     = CAMERA_PIN_HREF,

        /* SCCB (I2C) 控制线 */
        .pin_sccb_sda = CAMERA_PIN_SIOD,
        .pin_sccb_scl = CAMERA_PIN_SIOC,
        .sccb_i2c_port = 0,     /* 使用 I2C_NUM_0 */

        /* 电源与复位 */
        .pin_pwdn     = CAMERA_PIN_PWDN,
        .pin_reset    = CAMERA_PIN_RESET,

        /* 时钟频率 */
        .xclk_freq_hz = CAMERA_XCLK_FREQ_HZ,

        /* LEDC 通道 (esp32-camera 内部用于生成 XCLK) */
        .ledc_timer   = LEDC_TIMER_1,     /* 避开舵机 Timer 0 */
        .ledc_channel = LEDC_CHANNEL_2,

        /* ── 像素格式与分辨率 ──
         * 选择 PIXFORMAT_RGB565：ESP-DL HumanFaceDetect 直接支持 RGB565 输入，
         * 无需额外灰度转换，省去 CPU 开销。
         * (若使用 PIXFORMAT_GRAYSCALE 则更省 PSRAM 带宽，
         *  但需确认 ESP-DL preprocessor 支持 GRAY 格式。) */
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size   = FRAMESIZE_QVGA,   /* 320×240 */
        .jpeg_quality = 12,               /* JPEG 模式时有效 */

        /* ── 帧缓冲配置 ──
         * 使用 psramFound() 决定帧缓冲位置：
         *   有 PSRAM → CAMERA_FB_IN_PSRAM (8MB Octal, 充裕)
         *   无 PSRAM → CAMERA_FB_IN_DRAM  (内建 SRAM, 仅够单帧) */
        .fb_count    = esp_psram_is_initialized() ? CAMERA_FB_COUNT : 1,
        .fb_location = esp_psram_is_initialized()
                           ? CAMERA_FB_IN_PSRAM
                           : CAMERA_FB_IN_DRAM,

        /* grab_mode: WHEN_EMPTY — 帧缓冲空闲时才写入，
         * 配合双缓冲实现无丢帧流水线。 */
        .grab_mode   = CAMERA_GRAB_WHEN_EMPTY,
    };

    /* ── 2. 执行摄像头初始化 ── */
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "  CAMERA INIT FAILED: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "  PSRAM: %s",
                 esp_psram_is_initialized() ? "OK" : "NOT FOUND");
        ESP_LOGE(TAG, "  → System will restart in 3 seconds...");
        ESP_LOGE(TAG, "========================================");

        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();   /* 初始化失败，重启 */
        return;           /* 不会到达 */
    }

    /* ── 3. 获取传感器句柄，关闭镜像 (人脸追踪正向) ── */
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_hmirror(s, 0);   /* 关闭水平镜像 */
        /* OV2640 默认输出即可，无需额外设置 */
    }

    bool has_psram = esp_psram_is_initialized();
    ESP_LOGI(TAG, "Camera OK: QVGA RGB565, XCLK=%dHz, fb_count=%d, PSRAM=%s, fb_loc=%s",
             CAMERA_XCLK_FREQ_HZ,
             config.fb_count,
             has_psram ? "YES" : "NO",
             has_psram ? "PSRAM" : "DRAM");
}

/* ======================================================================== */
/*                          cam_task                                         */
/* ======================================================================== */

void cam_task(void *arg)
{
    ESP_LOGI(TAG, "Camera capture task started on Core %d", xPortGetCoreID());

    /* FPS 统计变量 */
    int64_t last_fps_time = esp_timer_get_time();
    int frame_count = 0;
    float fps = 0.0f;

    while (1) {
        /* ── 获取一帧 ── */
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "fb_get() returned NULL, retrying...");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* ── 帧有效性检查 ──
         * 确保 RGB565 格式且尺寸匹配 QVGA。
         * 异常帧直接归还跳过。 */
        if (fb->format != PIXFORMAT_RGB565 ||
            fb->width  != FRAME_WIDTH      ||
            fb->height != FRAME_HEIGHT) {
            ESP_LOGW(TAG, "Unexpected frame: format=%d %dx%d (expected RGB565 %dx%d)",
                     fb->format, fb->width, fb->height,
                     FRAME_WIDTH, FRAME_HEIGHT);
            esp_camera_fb_return(fb);
            continue;
        }

        /* ── 发送帧指针到 detect_task ──
         * xQueueSend 非阻塞发送 (0 timeout)：
         *   队列满 → 丢弃当前帧 (流水线已饱和，丢帧优于阻塞采集)
         *           归还 fb 给驱动，继续下一帧采集。 */
        if (g_frame_queue != NULL) {
            BaseType_t sent = xQueueSend(g_frame_queue, &fb, 0);
            if (sent != pdTRUE) {
                /* 队列满：detect_task 还在处理上一帧，
                 * 丢弃当前帧以保持流水线流畅。 */
                esp_camera_fb_return(fb);
            }
        } else {
            /* 队列未初始化：直接归还 */
            esp_camera_fb_return(fb);
        }

        /* ── FPS 统计 (每秒打印一次) ── */
        frame_count++;
        int64_t now = esp_timer_get_time();
        if (now - last_fps_time >= 1000000) {   /* 1 秒 */
            fps = (float)frame_count * 1000000.0f / (float)(now - last_fps_time);
            ESP_LOGI(TAG, "Capture FPS: %.1f (queue=%d)", fps,
                     (int)uxQueueMessagesWaiting(g_frame_queue));
            frame_count = 0;
            last_fps_time = now;
        }

        /* 短暂让出 CPU (yield)，不阻塞采集循环 */
        taskYIELD();
    }

    /* 不应到达 */
    vTaskDelete(NULL);
}
