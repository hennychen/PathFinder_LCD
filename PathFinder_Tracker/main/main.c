#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tracker_config.h"
#include "drv_es7210.h"
#include "drv_ws2812.h"
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

    ret = drv_es7210_init();
    if (ret != ESP_OK) {
        printf("[%s] ES7210 init FAILED: %s\n", TAG, esp_err_to_name(ret));
        return;
    }

    sound_localizer_init(ES7210_SAMPLE_RATE);
    sound_localizer_set_threshold(-2.7f);

    static float mic_data[4][256];
    while (1) {
        if (drv_es7210_read(mic_data) == ESP_OK) {
            localization_result_t result = sound_localizer_compute(mic_data);
            if (result.valid) {
                printf("angle: %.1f deg (conf=%.2f)\n", result.angle, result.confidence);
                drv_ws2812_show_angle(result.angle);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
