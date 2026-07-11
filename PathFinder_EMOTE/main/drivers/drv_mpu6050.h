/**
 * @file drv_mpu6050.h
 * @brief MPU-6050 6DOF 加速度计+陀螺仪 I2C 驱动
 *
 * 器件地址：0x68 (AD0=GND) 或 0x69 (AD0=VCC)
 * 量程：加速度 ±2g，陀螺仪 ±250°/s
 */
#ifndef DRV_MPU6050_H
#define DRV_MPU6050_H

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    float accel[3];    /**< ax, ay, az 单位 g */
    float gyro[3];     /**< gx, gy, gz 单位 °/s */
    float temp;        /**< 芯片温度 °C */
} mpu6050_data_t;

/**
 * @brief 初始化 MPU6050
 * @param bus  I2C-1 总线句柄
 * @param addr 器件地址 (通常 0x68)
 */
esp_err_t drv_mpu6050_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 读取加速度、陀螺仪、温度（单次读取 14 字节）
 */
esp_err_t drv_mpu6050_read(mpu6050_data_t *out);

#endif /* DRV_MPU6050_H */
