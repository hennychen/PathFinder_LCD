/**
 * @file drv_uv_adc.h
 * @brief GUVA-S12SD 紫外线传感器 ADC 驱动
 *
 * 模拟输出 → ADC 读取 → 电压 → UV 指数换算
 * GUVA-S12SD: 输出电压 mV ≈ UV指数 × ... (近似线性)
 */
#ifndef DRV_UV_ADC_H
#define DRV_UV_ADC_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

typedef struct {
    uint16_t raw;      /**< ADC 原始值 */
    float voltage;     /**< 电压 V */
    float uv_index;    /**< UV 指数 0-11+ */
} uv_data_t;

/**
 * @brief 初始化 UV ADC 通道
 * @param unit  ADC 单元 (ADC_UNIT_1)
 * @param channel ADC 通道 (如 ADC_CHANNEL_3 = GPIO3)
 */
esp_err_t drv_uv_init(adc_unit_t unit, adc_channel_t channel);

/**
 * @brief 读取 UV 数据
 */
esp_err_t drv_uv_read(uv_data_t *out);

#endif /* DRV_UV_ADC_H */
