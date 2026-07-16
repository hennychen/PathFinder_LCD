#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tracker_config.h"
#include "drv_es7210.h"
#include "drv_ws2812.h"
#include "drv_servo.h"
#include "drv_uart_comm.h"
#include "sound_localizer.h"

static const char *TAG = "tracker_main";

void app_main(void)
{
    printf("[%s] PathFinder_Tracker booting on ESP32-S3 N16R8\n", TAG);

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

    ret = drv_es7210_init();
    if (ret != ESP_OK) {
        printf("[%s] ES7210 init FAILED: %s\n", TAG, esp_err_to_name(ret));
        return;
    }

    sound_localizer_init(ES7210_SAMPLE_RATE);
    sound_localizer_set_threshold(-2.7f);

    static float mic_data[4][256];
    uint32_t invalid_frame_count = 0;

    while (1) {
        if (drv_es7210_read(mic_data) == ESP_OK) {
            localization_result_t result = sound_localizer_compute(mic_data);
            if (result.valid) {
                printf("angle: %.1f deg (conf=%.2f)\n", result.angle, result.confidence);
                drv_ws2812_show_angle(result.angle);
                float pan = drv_servo_angle_from_sound(result.angle);
                drv_servo_set_angle(SERVO_PAN, pan);
                /* Forward valid angle to Board A (PathFinder_EMOTE). */
                drv_uart_send_angle(result.angle, 1);
            } else {
                /* Periodically notify Board A that no sound source was
                   detected (every 50 invalid frames). */
                if (++invalid_frame_count >= 50) {
                    invalid_frame_count = 0;
                    drv_uart_send_angle(0, 0);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
