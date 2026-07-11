/**
 * @file drv_aht20.h
 * @brief AHT20 温湿度传感器 I2C 驱动
 *
 * 器件地址：0x38
 * 测量范围：温度 -40~85°C，湿度 0~100%RH
 */
#ifndef DRV_AHT20_H
#define DRV_AHT20_H

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    float temperature;  /**< 温度 °C */
    float humidity;     /**< 湿度 %RH */
} aht20_data_t;

/**
 * @brief 初始化 AHT20
 * @param bus  I2C-1 总线句柄
 * @param addr 器件地址 (通常 0x38)
 */
esp_err_t drv_aht20_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 读取温湿度（阻塞约 80ms）
 */
esp_err_t drv_aht20_read(aht20_data_t *out);

#endif /* DRV_AHT20_H */
