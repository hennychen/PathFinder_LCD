/**
 * @file sensor_manager.h
 * @brief 传感器管理器 — 创建 I2C-1 总线、初始化驱动、双任务采样
 *
 * 架构：
 *   env_task @1Hz  → AHT20 + BMP280 + UV  (环境数据)
 *   imu_task @50Hz → MPU6050              (姿态数据)
 *
 * 线程安全：env/imu 各一把互斥锁
 */
#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "drv_aht20.h"
#include "drv_bmp280.h"
#include "drv_mpu6050.h"
#include "drv_uv_adc.h"

/* ── 数据快照结构 ── */

/** 环境数据快照 (低频更新 1Hz) */
typedef struct {
    aht20_data_t  aht20;    /**< 温湿度 */
    bmp280_data_t bmp280;   /**< 气压/海拔 */
    uv_data_t     uv;       /**< UV指数 */
    int64_t       timestamp_us;
} env_snapshot_t;

/** IMU 原始数据 (高频更新 50Hz，只保持最新值) */
typedef struct {
    mpu6050_data_t imu;
    int64_t        timestamp_us;
} imu_snapshot_t;

/**
 * @brief 初始化传感器管理器
 *        - 创建 I2C-1 总线 (GPIO12=SCL, GPIO14=SDA)
 *        - 初始化 4 个传感器驱动
 *        - 启动 env_task + imu_task
 * @return ESP_OK 或错误码
 */
esp_err_t sensor_manager_init(i2c_master_bus_handle_t bus);

/**
 * @brief 获取最新环境数据快照 (线程安全)
 */
esp_err_t sensor_manager_get_env(env_snapshot_t *out);

/**
 * @brief 获取最新 IMU 数据快照 (线程安全)
 */
esp_err_t sensor_manager_get_imu(imu_snapshot_t *out);

#endif /* SENSOR_MANAGER_H */
