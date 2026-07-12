/**
 * @file drv_bmp280.h
 * @brief BMP280 气压/温度/海拔传感器 I2C 驱动
 *
 * 器件地址：0x76 或 0x77
 * 包含校准系数读取 + 补偿公式 + 海拔换算
 */
#ifndef DRV_BMP280_H
#define DRV_BMP280_H

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    float pressure;    /**< 气压 Pa */
    float temperature; /**< 芯片内部温度 °C（用于补偿） */
    float altitude;    /**< 海拔 m（基于标准海平面气压换算） */
} bmp280_data_t;

/** 校准参数 */
typedef struct {
    float pressure_offset_pa;  /**< 气压偏移补偿 (Pa)，正=加，负=减 */
    float sea_level_pa;        /**< 当地海平面参考气压 (Pa)，默认 101325 */
} bmp280_calib_config_t;

/**
 * @brief 初始化 BMP280
 * @param bus  I2C-1 总线句柄
 * @param addr 器件地址 (通常 0x76)
 */
esp_err_t drv_bmp280_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 设置海平面参考气压（用于海拔校准）
 * @param pa 海平面气压 Pa（默认 101325）
 */
void drv_bmp280_set_sea_level(float pa);

/**
 * @brief 设置气压偏移补偿
 * @param offset_pa 偏移量 (Pa)，正值表示加到读数上
 */
void drv_bmp280_set_pressure_offset(float offset_pa);

/**
 * @brief 根据已知海拔反推当地海平面气压
 *        公式: P0 = P / (1 - h/44330)^5.255
 * @param known_altitude_m 已知海拔 (m)
 * @param current_pressure_pa 当前实测气压 (Pa)
 * @return 反推出的海平面气压 (Pa)
 */
float drv_bmp280_calc_sea_level(float known_altitude_m, float current_pressure_pa);

/**
 * @brief 应用完整校准配置
 */
void drv_bmp280_apply_calib(const bmp280_calib_config_t *calib);

/**
 * @brief 读取气压、温度、海拔
 */
esp_err_t drv_bmp280_read(bmp280_data_t *out);

#endif /* DRV_BMP280_H */
