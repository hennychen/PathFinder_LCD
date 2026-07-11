/**
 * @file drv_uv_adc.c
 * @brief GUVA-S12SD 紫外线传感器 ADC 驱动实现
 *
 * GUVA-S12SD 特性：
 *   - 工作电压 2.7V~5.5V
 *   - 输出电压与 UV 指数近似线性
 *   - 典型: 电压 V → UV index ≈ V × ... (需实测标定)
 *
 * 标定公式 (经验值，可后续微调):
 *   uv_index = voltage_V × 10.0  (粗略，1V ≈ UV 10)
 *   clamp 到 [0, 15]
 */
#include "drv_uv_adc.h"
#include "esp_log.h"

static const char *TAG = "drv_uv_adc";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_channel_t             s_adc_channel;

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

    ESP_LOGI(TAG, "UV ADC 初始化完成 (unit=%d, ch=%d)", unit, channel);
    return ESP_OK;
}

esp_err_t drv_uv_read(uv_data_t *out)
{
    if (s_adc_handle == NULL || out == NULL) return ESP_ERR_INVALID_STATE;

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
    if (ret != ESP_OK) return ret;

    out->raw = (uint16_t)raw;

    /* 12bit ADC, 12dB 衰减 → 满量程约 3.3V
     * 使用经验值: 满量程 4095 ≈ 3.3V */
    out->voltage = (float)raw * 3.3f / 4095.0f;

    /* GUVA-S12SD: 粗略 UV 指数换算 (1V ≈ UV 10)
     * 注意: 此公式需要根据实际传感器标定调整 */
    out->uv_index = out->voltage * 10.0f;
    if (out->uv_index < 0) out->uv_index = 0;
    if (out->uv_index > 15.0f) out->uv_index = 15.0f;

    return ESP_OK;
}
