#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tracker_config.h"

static const char *TAG = "tracker_main";

void app_main(void)
{
    printf("[%s] PathFinder_Tracker booting on ESP32-S3 N16R8\n", TAG);
    printf("[%s] Phase 1: Hardware driver verification\n", TAG);

    while (1) {
        printf("[%s] heartbeat\n", TAG);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
