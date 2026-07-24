/**
 * @file drv_dust_gp2y.h
 * @brief Sharp GP2Y1010AU0F (DFRobot SEN0144) 粉尘传感器驱动
 *
 * 工作原理：
 *   红外 LED 脉冲照射 → 光电晶体管检测粉尘反射光 → 模拟电压输出
 *   输出电压与粉尘浓度成正比 (灵敏度 ~0.5V / 0.1mg/m³)
 *
 * 采样时序 (每次脉冲 ~10ms)：
 *   LED=LOW → 280µs → ADC采样 → 40µs → LED=HIGH → 9680µs
 *
 * 硬件接线 (DFRobot Gravity 适配器, 4-pin: VCC/GND/A/D)：
 *   VCC  → 5V   (适配器内置 150Ω + 220µF)
 *   GND  → GND
 *   D    → GPIO39 (LED 数字脉冲控制)
 *   A    → GPIO1  (ADC1_CH0, 模拟电压输出)
 */
#ifndef DRV_DUST_GP2Y_H
#define DRV_DUST_GP2Y_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

/**
 * @brief 粉尘传感器数据
 */
typedef struct {
    uint16_t raw;           /**< ADC 原始值 (过采样后) */
    float    voltage;       /**< 校准电压 V */
    float    density_mgm3;  /**< 粉尘浓度 mg/m³ */
    uint8_t  aqi_level;     /**< 空气质量等级 0~4 (优/良/轻度/中度/重度) */
} dust_data_t;

/**
 * @brief 初始化粉尘传感器
 * @param unit     ADC 单元 (必须 ADC_UNIT_1, WiFi 开启时 ADC2 不可用)
 * @param adc_ch   ADC 通道 (ADC_CHANNEL_0 = GPIO1)
 * @param led_gpio LED 脉冲控制引脚 (GPIO_NUM_39)
 */
esp_err_t drv_dust_init(adc_unit_t unit, adc_channel_t adc_ch, gpio_num_t led_gpio);

/**
 * @brief 读取粉尘传感器数据
 *        执行 10 次 LED 脉冲采样取平均 (~100ms)
 */
esp_err_t drv_dust_read(dust_data_t *out);

#endif /* DRV_DUST_GP2Y_H */
