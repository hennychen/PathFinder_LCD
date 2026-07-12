/**
 * @file drv_uv_adc.c
 * @brief GUVA-S12SD 紫外线传感器 ADC 驱动实现
 *
 * 改进版：ADC校准 + 1024次过采样 + 查表法UV换算
 *
 * GUVA-S12SD 特性：
 *   - 工作电压 2.7V~5.5V（推荐 5V）
 *   - 输出电压 0~1V，对应 UV index 0~11+
 *   - UV index 与输出电压为非线性关系，使用查表法
 *
 * UV Index 查表（来源：CJMCU 官方示例代码）：
 *   Vout(mV)  →  UV Index
 *   < 50      →  0
 *   50~227    →  1
 *   227~318   →  2
 *   318~408   →  3
 *   408~503   →  4
 *   503~606   →  5
 *   606~696   →  6
 *   696~795   →  7
 *   795~881   →  8
 *   881~976   →  9
 *   976~1079  →  10
 *   >= 1079   →  11
 */
#include "drv_uv_adc.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_uv_adc";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_adc_cali_handle = NULL;
static adc_channel_t             s_adc_channel;

/* ── UV Index 查表法（基于 CJMCU 官方 mV → UV 映射）── */
static float uv_lookup_table(float voltage_mv)
{
    if (voltage_mv < 50)   return 0;
    if (voltage_mv < 227)  return 1;
    if (voltage_mv < 318)  return 2;
    if (voltage_mv < 408)  return 3;
    if (voltage_mv < 503)  return 4;
    if (voltage_mv < 606)  return 5;
    if (voltage_mv < 696)  return 6;
    if (voltage_mv < 795)  return 7;
    if (voltage_mv < 881)  return 8;
    if (voltage_mv < 976)  return 9;
    if (voltage_mv < 1079) return 10;
    return 11;
}

esp_err_t drv_uv_init(adc_unit_t unit, adc_channel_t channel)
{
    /* 初始化 ADC 单元 */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unit,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 单元初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置通道: 12bit, 12dB 衰减 (满量程 ~3.3V) */
    s_adc_channel = channel;
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ADC 校准 (曲线拟合) — 消除 ESP32-S3 ADC 非线性误差 */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC 校准成功 (curve fitting)");
    } else {
        ESP_LOGW(TAG, "ADC 校准失败(%s)，使用原始值换算", esp_err_to_name(ret));
        s_adc_cali_handle = NULL;
    }

    /* 诊断：检查 GPIO3 数字电平 */
    int gpio_level = gpio_get_level(3);
    ESP_LOGI(TAG, "UV ADC 初始化完成 (unit=%d, ch=%d) | GPIO3 digital level=%d", unit, channel, gpio_level);

    /* 丢弃前几次 ADC 读取（ADC 稳定需要几次空读） */
    int discard_raw = 0;
    for (int i = 0; i < 5; i++) {
        adc_oneshot_read(s_adc_handle, s_adc_channel, &discard_raw);
        ESP_LOGI(TAG, "ADC warm-up read #%d: raw=%d", i, discard_raw);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

esp_err_t drv_uv_read(uv_data_t *out)
{
    if (s_adc_handle == NULL || out == NULL) return ESP_ERR_INVALID_STATE;

    /* 1024次过采样平均 — 大幅降低噪声，提升有效分辨率 */
    int sum = 0;
    int sample_count = 1024;
    for (int i = 0; i < sample_count; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "adc_oneshot_read 失败: %s", esp_err_to_name(ret));
            return ret;
        }
        sum += raw;
    }
    int avg_raw = sum / sample_count;
    out->raw = (uint16_t)avg_raw;

    /* 校准电压换算 */
    int voltage_mv = 0;
    if (s_adc_cali_handle != NULL) {
        /* 使用校准数据换算 (mV) */
        adc_cali_raw_to_voltage(s_adc_cali_handle, avg_raw, &voltage_mv);
        out->voltage = (float)voltage_mv / 1000.0f;
    } else {
        /* 无校准时使用近似换算: 12bit + 12dB → 3.3V 满量程 */
        voltage_mv = (int)((float)avg_raw * 3300.0f / 4095.0f);
        out->voltage = (float)voltage_mv / 1000.0f;
    }

    /* UV Index 查表法 (基于 CJMCU 官方映射) */
    out->uv_index = uv_lookup_table((float)voltage_mv);

    ESP_LOGI(TAG, "ADC avg_raw=%d → %dmV → UV=%.0f", avg_raw, voltage_mv, out->uv_index);

    return ESP_OK;
}
