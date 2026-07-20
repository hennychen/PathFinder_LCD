#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "tracker_config.h"
#include "drv_es7210.h"
#include "drv_ws2812.h"
#include "drv_servo.h"
#include "drv_uart_comm.h"
#include "drv_ov2640.h"
#include "sound_localizer.h"
#include "tracker_state_machine.h"
#include "vision_task.h"
#include "web_viewer.h"
#include "mesh_node.h"
#include "mesh_espnow.h"
#include "comm_link.h"
#include "mesh_protocol.h"
#include "esp_timer.h"

static const char *TAG = "tracker_main";
static tracker_ctx_t s_tracker_ctx;

/* === 摄像头调试开关 ===
 * 1 = 跳过 Mesh, 强制启动 WiFi Web Viewer (连 "TrackerDev" / 12345678,
 *     浏览器打开 http://192.168.4.1/ 查看实时画面)
 * 0 = 正常模式 (Mesh + A板通信)
 */
#define TRACKER_DEBUG_CAM 0

/* ===================== Mesh / ESP-NOW 接收状态 ===================== */
static int64_t s_last_board_a_us = 0;   /* 上次收到 A 板消息的时间戳 */

/* 处理来自 A 板的控制指令 */
static void handle_board_a_msg(const uint8_t *src_mac, const uint8_t *raw, int raw_len)
{
    mesh_msg_t msg;
    if (!mesh_msg_parse(raw, raw_len, &msg)) {
        return;  /* CRC 校验失败 */
    }

    s_last_board_a_us = esp_timer_get_time();

    switch (msg.msg_type) {
    case MSG_SERVO_CTRL:
        /* payload: [pan_u16_le][tilt_u16_le]  (0.1° 固定点) */
        if (msg.payload_len >= 4) {
            uint16_t pan_fixed  = (uint16_t)(msg.payload[0] | (msg.payload[1] << 8));
            uint16_t tilt_fixed = (uint16_t)(msg.payload[2] | (msg.payload[3] << 8));
            float pan  = pan_fixed  / 10.0f;
            float tilt = tilt_fixed / 10.0f;
            drv_servo_set_angle(SERVO_PAN, pan);
            drv_servo_set_angle(SERVO_TILT, tilt);
            ESP_LOGI(TAG, "Servo override from A: pan=%.1f tilt=%.1f", pan, tilt);
        }
        break;

    case MSG_MODE_SWITCH:
        /* payload: [mode_u8]  (tracker_state_t 枚举值) */
        if (msg.payload_len >= 1) {
            uint8_t mode = msg.payload[0];
            ESP_LOGI(TAG, "Mode switch from A: state=%d", mode);
            /* 直接写入状态机的当前状态，实现远程模式控制 */
            s_tracker_ctx.current_state = (tracker_state_t)mode;
        }
        break;

    case MSG_HEARTBEAT:
        /* A 板心跳确认，已通过 s_last_board_a_us 记录 */
        break;

    default:
        break;
    }
}

/* ESP-NOW 接收回调 (在 WiFi 任务上下文中运行) */
static void on_espnow_rx(const uint8_t *src_mac, const uint8_t *data, int data_len)
{
    handle_board_a_msg(src_mac, data, data_len);
}

/* Mesh P2P 接收回调 (在 mesh_rx_task 中运行) */
static void on_mesh_rx(const uint8_t *from_mac, const uint8_t *data, int data_len)
{
    handle_board_a_msg(from_mac, data, data_len);
}

/* Pull PA_EN high at the very start so the AcousticEye board (ES7210 +
   WS2812 + audio circuitry) is powered before any peripheral init. */
static void board_power_enable(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PA_EN_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PA_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  /* let power rails stabilise */
    printf("[%s] PA_EN HIGH on GPIO%d\n", TAG, PA_EN_GPIO);
}

void app_main(void)
{
    printf("[%s] PathFinder_Tracker Phase 3 + Mesh starting\n", TAG);

    /* ---- 0. NVS init (required for Wi-Fi + Mesh) ---- */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    printf("[%s] NVS init: %s\n", TAG, esp_err_to_name(nvs_ret));

    /* ---- 1. Power up AcousticEye board FIRST ---- */
    board_power_enable();

    /* WS2812 LED ring */
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

    /* ---- 3. Mesh 子节点初始化 (CHILD 角色, 无需路由器凭据) ---- */
    /* CHILD 只需 MESH_ID + MESH_AP_PASSWD 即可寻找 ROOT 并加入网络 */
#if TRACKER_DEBUG_CAM
    printf("[%s] === CAMERA DEBUG MODE — Mesh skipped, Web Viewer will start ===\n", TAG);
    esp_err_t mesh_ret = ESP_FAIL;  /* 模拟 Mesh 失败, 触发 Web Viewer */
#else
    esp_err_t mesh_ret = mesh_node_init(MESH_ROLE_CHILD);
    if (mesh_ret == ESP_OK) {
        mesh_ret = mesh_node_start();
        if (mesh_ret == ESP_OK) {
            printf("[%s] Mesh CHILD started, waiting for ROOT...\n", TAG);

            /* ESP-NOW 初始化 (在 Mesh/WiFi 启动后, 自动适配 Mesh 信道) */
            mesh_espnow_init();
            mesh_espnow_register_rx_cb(on_espnow_rx);

            /* 注册 Mesh P2P 接收回调 */
            mesh_node_register_rx_cb(on_mesh_rx);

            /* 初始化通信链路 (获取 ROOT MAC, 注册 ESP-NOW peer) */
            comm_link_init();
        } else {
            printf("[%s] Mesh start FAILED: %s\n", TAG, esp_err_to_name(mesh_ret));
        }
    } else {
        printf("[%s] Mesh init FAILED: %s\n", TAG, esp_err_to_name(mesh_ret));
    }
#endif

    /* ---- 4. OV2640 camera — initialise before ES7210 ---- */
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

    /* Web viewer 仅在 Mesh 未启动时启用 (两者都占用 WiFi, 互斥) */
    if (mesh_ret != ESP_OK) {
        ret = web_viewer_start();
        if (ret != ESP_OK) {
            printf("[%s] Web viewer start FAILED (non-fatal): %s\n", TAG, esp_err_to_name(ret));
        }
    } else {
        printf("[%s] Web viewer SKIPPED (Mesh is using WiFi)\n", TAG);
    }

    ret = drv_es7210_init();
    bool es7210_ok = (ret == ESP_OK);
    if (!es7210_ok) {
        printf("[%s] ES7210 init FAILED (non-fatal): %s\n", TAG, esp_err_to_name(ret));
        printf("[%s] Audio tracking disabled. Check I2C wiring (SDA=38,SCL=39,ADDR=0x40)\n", TAG);
    } else {
        sound_localizer_init(ES7210_SAMPLE_RATE);
        sound_localizer_set_threshold(-2.7f);
    }

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
    uint32_t heartbeat_counter = 0;

    uint32_t audio_diag_counter = 0;
    uint32_t audio_ok_count = 0;
    uint32_t audio_fail_count = 0;

    while (1) {
        if (es7210_ok && drv_es7210_read(mic_data) == ESP_OK) {
            audio_ok_count++;
            localization_result_t result = sound_localizer_compute(mic_data);
            tracker_sm_step(&s_tracker_ctx, result.angle, result.valid);
        } else if (es7210_ok) {
            audio_fail_count++;
        }

        /* Audio diagnostic every ~5 seconds */
        if (es7210_ok && ++audio_diag_counter >= 1000) {
            audio_diag_counter = 0;
            /* Compute raw RMS for diagnostics */
            float max_peak = 0.0f;
            for (int ch = 0; ch < 4; ch++) {
                float mean = 0.0f;
                for (int i = 0; i < 256; i++) mean += mic_data[ch][i];
                mean /= 256.0f;
                float rms = 0.0f;
                for (int i = 0; i < 256; i++) {
                    float d = mic_data[ch][i] - mean;
                    rms += d * d;
                    float a = fabsf(mic_data[ch][i]);
                    if (a > max_peak) max_peak = a;
                }
                rms = sqrtf(rms / 256.0f);
                if (ch == 0) {
                    float activity = (rms > 1e-5f) ? log10f(rms) : -5.0f;
                    printf("[%s] rms[0]=%.5f act=%.2f peak=%.4f thr=%.1f state=%d\n",
                           TAG, rms, activity, max_peak, -2.7f,
                           s_tracker_ctx.current_state);
                }
            }
            printf("[%s] audio: ok=%lu fail=%lu\n",
                   TAG, (unsigned long)audio_ok_count, (unsigned long)audio_fail_count);
        }

        /* Check for new vision data (non-blocking).  We peek every ~5 ms
           but the underlying queue is overwrite-mode so we always see
           the latest result if one exists. */
        if (vision_get_latest(&vmsg)) {
            tracker_sm_step_vision(&s_tracker_ctx, vmsg.face_found,
                                   vmsg.face_cx, vmsg.face_cy,
                                   vmsg.face_w, vmsg.face_h);
        }

        /* Heartbeat every ~500ms (100 loop iterations * ~5ms each).
           Also refresh root MAC for ESP-NOW peer registration. */
        if (++heartbeat_counter >= 1000) {
            heartbeat_counter = 0;
            /* Only attempt comm link if Mesh is running */
            if (mesh_ret == ESP_OK) {
                comm_link_send_heartbeat();
            }
        }

        /* Yield to IDLE task to feed watchdog. vTaskDelay(1) gives the
           IDLE task at least one tick (~1ms) to run esp_task_wdt_reset(). */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
