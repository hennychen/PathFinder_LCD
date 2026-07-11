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
 * @brief 读取气压、温度、海拔
 */
esp_err_t drv_bmp280_read(bmp280_data_t *out);

#endif /* DRV_BMP280_H */
