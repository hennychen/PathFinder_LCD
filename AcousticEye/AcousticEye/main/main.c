#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "calc_direction.h"
#include "ws2812.h"

#define TAG_APP "AcousticEye"

/**
 * @file main.c
 * @brief AcousticEye 应用入口。
 *
 * 本文件负责把声源定位算法和 WS2812 方向指示灯串联起来：
 * 1. 初始化四通道麦克风采集；
 * 2. 周期性读取 I2S/TDM 音频数据；
 * 3. 调用 GCC-PHAT 声源定位算法计算角度；
 * 4. 将角度映射到 36 颗环形 LED 上显示。
 */

/**
 * @brief 四路麦克风采样缓存。
 *
 * 第一维表示麦克风通道，第二维表示当前处理帧内的采样点。
 * 数组长度必须与 calc_direction.h 中的 PDM_SAMPLE_SIZE 保持一致。
 */
float i2s_mic_data[4][PDM_SAMPLE_SIZE] = {0};

/**
 * @brief 兼容旧接口保留的 I2C 寄存器数据缓存。
 */
extern uint8_t I2CRegData[8];

/**
 * @brief 将 0~360 度角度转换为易读方向字符串。
 *
 * @param angle 声源方向角，单位为度。小于 0 表示无效角度。
 * @return const char* 方向文本，例如 front、left、back-right。
 */
static const char *angle_to_dir(float angle)
{
    if (angle < 0.0f) return "invalid";
    if (angle >= 337.5f || angle < 22.5f) return "right";
    if (angle < 67.5f) return "front-right";
    if (angle < 112.5f) return "front";
    if (angle < 157.5f) return "front-left";
    if (angle < 202.5f) return "left";
    if (angle < 247.5f) return "back-left";
    if (angle < 292.5f) return "back";
    return "back-right";
}


static void print_mic_data_500ms(void)
{
    static TickType_t last_tick = 0;
    TickType_t now = xTaskGetTickCount();

    if ((now - last_tick) < pdMS_TO_TICKS(500)) {
        return;
    }
    last_tick = now;

    float rms[4] = {0};
    float peak[4] = {0};

    calcRawLevels(rms);

    for (int ch = 0; ch < 4; ch++) {
        for (int i = 0; i < PDM_SAMPLE_SIZE; i++) {
            float v = fabsf(i2s_mic_data[ch][i]);
            if (v > peak[ch]) {
                peak[ch] = v;
            }
        }
    }

    printf("mic rms=[%.6f, %.6f, %.6f, %.6f] "
           "peak=[%.6f, %.6f, %.6f, %.6f] "
           "s0=[%.4f, %.4f, %.4f, %.4f]\n",
           rms[0], rms[1], rms[2], rms[3],
           peak[0], peak[1], peak[2], peak[3],
           i2s_mic_data[0][0],
           i2s_mic_data[1][0],
           i2s_mic_data[2][0],
           i2s_mic_data[3][0]);
}


/**
 * @brief 音频定位主任务。
 *
 * @param arg FreeRTOS 任务参数，本任务未使用。
 *
 * 任务流程：
 * 1. 初始化 ES7210 + I2S 四通道麦克风；
 * 2. 初始化 FFT 窗函数和活动阈值；
 * 3. 持续读取一帧麦克风数据并计算声源角度；
 * 4. 有效角度映射为 LED 下标，无效角度持续一段时间后清灯。
 */
static void audio_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG_APP, "Initializing ES7210 + I2S...");
    initI2SMics();
    prepareWindow(48000.0f);

    /* 降低活动阈值，提高灵敏度（ES7210 ADC音量已+30dB） */
    setActivityValue(-4.0f);

    ESP_LOGI(TAG_APP, "Audio pipeline ready, entering main loop");

    /* 启动调试：每 500ms 打印一次 4 通道 RMS */
    uint32_t wsnumber = 0;
    int cnt = 0;
    uint32_t loop_count = 0;
    while (1) {
        i2s_read_mics();
        float angle = calcAngle();

        /* 详细麦克风数据输出（含原始样本值，每 500ms） */
        print_mic_data_500ms();

        /* 每 500ms 打印一次 4 通道 RMS 和活动量 */
        if (loop_count % 25 == 0) {
            float rms[4] = {0};
            calcRawLevels(rms);
            calc_debug_info_t info;
            getCalcDebugInfo(&info);
            ESP_LOGI(TAG_APP, "rms=[%.5f,%.5f,%.5f,%.5f] act=%.3f angle=%.1f",
                     rms[0], rms[1], rms[2], rms[3],
                     info.activity_value, info.angle_final);
        }
        loop_count++;

        if (angle >= 0.0f) {
            wsnumber = (uint32_t)(angle / 10.0f) % 36;
            if(wsnumber >= LED_STRIP_LED_COUNT) wsnumber = LED_STRIP_LED_COUNT - 1;
            ws2812_clear();
            ws2812_ctrl(wsnumber, 255, 0, 0);
            ws2812_show();
            ESP_LOGI(TAG_APP, "LED[%lu] angle=%.1f deg", (unsigned long)wsnumber, angle);
            cnt = 0;
        }
        else{
            cnt++;
            if(cnt >= 50){
                ws2812_clear();
                ws2812_show();
                cnt = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief ESP-IDF 应用入口。
 *
 * 初始化 WS2812 灯带后创建音频定位任务。
 */
void app_main(void)
{
    ws2812_init();
    xTaskCreate(audio_task, "audio_task", 12288, NULL, 5, NULL);
}
