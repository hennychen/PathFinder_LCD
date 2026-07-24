/**
 * @file main_pipeline.c
 * @brief Face Tracker Pipeline — 主入口 (app_main)
 *
 * =========================================================================
 *  系统启动流程：
 *    1. 打印启动 Banner（芯片型号、PSRAM 信息、空闲堆内存）
 *    2. 创建两个 FreeRTOS 队列 (frame_queue + control_queue)
 *    3. 初始化摄像头 (camera_init)
 *    4. 初始化舵机 PWM (servo_pid_init)
 *    5. 创建三个流水线任务（跨核分配）：
 *         cam_task     → CPU 0  (帧采集)
 *         detect_task  → CPU 1  (ESP-DL 推理)
 *         control_task → CPU 0  (PID 舵机控制)
 *
 *  流水线拓扑：
 *    ┌──────────┐  frame_queue(2)  ┌────────────┐  control_queue(4)  ┌─────────────┐
 *    │ cam_task │ ───────────────▶ │ detect_task│ ─────────────────▶ │ control_task│
 *    │  CPU 0   │  camera_fb_t*    │   CPU 1    │  face_offset_t     │    CPU 0    │
 *    └──────────┘                  └────────────┘                    └─────────────┘
 *
 *  性能目标：
 *    采集 FPS:  ~30 FPS (OV2640 QVGA 双缓冲)
 *    推理 FPS:  ~15-22 FPS (MSRMNP_S8_V1, ESP32-S3 @240MHz)
 *    控制频率:  50 Hz (20ms 周期)
 *    端到端延迟: <100ms (采集30ms + 推理44ms + 控制20ms)
 * =========================================================================
 */

#include "face_tracker.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"

#define TAG "Main"

void app_main(void)
{
    /* ════════════════════════════════════════════════════════════════
     *  1. 系统信息 Banner
     * ════════════════════════════════════════════════════════════════ */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size_mb = 0;
    esp_flash_get_size(NULL, &flash_size_mb);
    flash_size_mb /= (1024 * 1024);

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   Face Tracker Pipeline — ESP32-S3 + ESP-DL   ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  Chip:    ESP32-S3 rev %d, %d cores @ %d MHz ║",
             chip_info.revision, chip_info.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "║  Flash:   %lu MB %s",
             (unsigned long)flash_size_mb,
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "(embedded)" : "(external)");
    ESP_LOGI(TAG, "║  PSRAM:   %s (%lu KB)",
             esp_psram_is_initialized() ? "YES" : "NO",
             (unsigned long)(esp_psram_get_size() / 1024));
    ESP_LOGI(TAG, "║  Heap:    %lu KB free",
             (unsigned long)(esp_get_free_heap_size() / 1024));
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    /* ════════════════════════════════════════════════════════════════
     *  2. 创建 FreeRTOS 队列
     * ════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "Creating FreeRTOS queues...");

    /* 帧队列: cam_task → detect_task
     * 深度 = FRAME_QUEUE_LEN (2)，匹配双帧缓存 ping-pong */
    g_frame_queue = xQueueCreate(FRAME_QUEUE_LEN, sizeof(camera_fb_t *));
    if (g_frame_queue == NULL) {
        ESP_LOGE(TAG, "FATAL: Failed to create frame queue");
        esp_restart();
    }

    /* 控制队列: detect_task → control_task
     * 使用 xQueueOverwrite 写入，保证 control_task 始终获得最新偏移量。
     * 队列深度必须 ≥ 1 (xQueueOverwrite 要求)。 */
    g_control_queue = xQueueCreate(CONTROL_QUEUE_LEN, sizeof(face_offset_t));
    if (g_control_queue == NULL) {
        ESP_LOGE(TAG, "FATAL: Failed to create control queue");
        esp_restart();
    }

    ESP_LOGI(TAG, "Queues OK: frame(%d) + control(%d)",
             FRAME_QUEUE_LEN, CONTROL_QUEUE_LEN);

    /* ════════════════════════════════════════════════════════════════
     *  3. 初始化硬件外设
     * ════════════════════════════════════════════════════════════════ */

    /* 摄像头 (OV2640 DVP, QVGA RGB565, PSRAM 帧缓存) */
    camera_init();

    /* 双轴舵机 (LEDC PWM 50Hz 10-bit, Pan=GPIO47 + Tilt=GPIO14) */
    servo_pid_init();

    /* ════════════════════════════════════════════════════════════════
     *  4. 启动流水线任务 (跨核分配)
     * ════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "Starting pipeline tasks...");

    /* ── Stage 1: cam_task (CPU 0) ──
     * 摄像头采集 + 帧入队。固定在 Core 0，
     * 因为 esp_camera DVP 驱动内部使用 SPI DMA 与 CPU 0 绑定。 */
    BaseType_t ret1 = xTaskCreatePinnedToCore(
        cam_task,
        "cam_task",
        CAM_TASK_STACK_SIZE,    /* 8192 */
        NULL,
        CAM_TASK_PRIORITY,      /* 4 */
        NULL,
        CAM_TASK_CORE           /* 0 */
    );
    if (ret1 != pdPASS) {
        ESP_LOGE(TAG, "FATAL: Failed to create cam_task");
        esp_restart();
    }

    /* ── Stage 2: detect_task (CPU 1) ──
     * ESP-DL 神经网络推理。固定在 Core 1，
     * 独占一个核心执行 INT8 矩阵运算，不受其他任务干扰。
     * 栈大小 16384 (ESP-DL 推理需要较大栈空间)。 */
    BaseType_t ret2 = xTaskCreatePinnedToCore(
        detect_task,
        "detect_task",
        DETECT_TASK_STACK_SIZE,  /* 16384 */
        NULL,
        DETECT_TASK_PRIORITY,    /* 3 */
        NULL,
        DETECT_TASK_CORE         /* 1 */
    );
    if (ret2 != pdPASS) {
        ESP_LOGE(TAG, "FATAL: Failed to create detect_task");
        esp_restart();
    }

    /* ── Stage 3: control_task (CPU 0) ──
     * PID 控制 + 舵机 PWM 输出。固定在 Core 0，
     * 与 cam_task 共享 CPU 0 (两者负载均低，不会互相阻塞)。 */
    BaseType_t ret3 = xTaskCreatePinnedToCore(
        control_task,
        "control_task",
        CONTROL_TASK_STACK_SIZE, /* 4096 */
        NULL,
        CONTROL_TASK_PRIORITY,   /* 5 (最高) */
        NULL,
        CONTROL_TASK_CORE        /* 0 */
    );
    if (ret3 != pdPASS) {
        ESP_LOGE(TAG, "FATAL: Failed to create control_task");
        esp_restart();
    }

    /* ════════════════════════════════════════════════════════════════
     *  5. 启动完成
     * ════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Pipeline started successfully!               ║");
    ESP_LOGI(TAG, "║  cam_task(C0) → detect_task(C1) → ctrl(C0)   ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    /* app_main 返回后，FreeRTOS 空闲任务接管。
     * 三个流水线任务各自死循环运行。 */
}
