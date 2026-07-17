#include <stdio.h>
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
#include "mesh_node.h"
#include "mesh_espnow.h"
#include "comm_link.h"

static const char *TAG = "tracker_main";
static tracker_ctx_t s_tracker_ctx;

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

    /* ---- 3. Mesh 子节点初始化 (Phase 5, 暂时禁用) ---- */
    /* TODO: Mesh需要路由器SSID/PASSWORD配置, 暂时禁用避免crash */
    esp_err_t mesh_ret = ESP_ERR_NOT_SUPPORTED;
    printf("[%s] Mesh init SKIPPED (not configured yet)\n", TAG);

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
            printf("[%s] audio: ok=%lu fail=%lu state=%d\n",
                   TAG, (unsigned long)audio_ok_count, (unsigned long)audio_fail_count,
                   s_tracker_ctx.current_state);
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
