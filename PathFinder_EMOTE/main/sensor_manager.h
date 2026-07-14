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
 * @brief 预加载校准参数 (必须在 LCD DMA 启动前调用)
 *        NVS flash 读取与 RGB LCD bounce-buffer DMA ISR 不兼容
 */
void sensor_manager_preload_calib(void);

/**
 * @brief 获取最新环境数据快照 (线程安全)
 */
esp_err_t sensor_manager_get_env(env_snapshot_t *out);

/**
 * @brief 获取最新 IMU 数据快照 (线程安全)
 */
esp_err_t sensor_manager_get_imu(imu_snapshot_t *out);

/* ── 校准接口 ── */

/**
 * @brief 根据已知海拔校准气压计
 *        自动反推当地海平面气压并持久化到 NVS
 * @param known_altitude_m 已知海拔 (m)
 * @return ESP_OK 或错误码
 */
esp_err_t sensor_manager_calib_altitude(float known_altitude_m);

/**
 * @brief 设置气压偏移补偿并持久化
 * @param offset_pa 偏移量 (Pa)
 * @return ESP_OK 或错误码
 */
esp_err_t sensor_manager_calib_pressure_offset(float offset_pa);

/**
 * @brief 重置校准参数为默认值
 * @return ESP_OK 或错误码
 */
esp_err_t sensor_manager_calib_reset(void);

/**
 * @brief 获取当前校准参数
 */
bmp280_calib_config_t sensor_manager_get_calib(void);

#endif /* SENSOR_MANAGER_H */
