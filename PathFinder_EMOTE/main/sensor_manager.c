/**
 * @file sensor_manager.c
 * @brief 传感器管理器实现
 */
#include "sensor_manager.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

/* 前向声明：motion_engine */
#include "motion_engine.h"

static const char *TAG = "sensor_mgr";

/* ── 采样参数 ── */
#define ENV_STACK_SIZE    (4 * 1024)
#define IMU_STACK_SIZE    (6 * 1024)
#define ENV_TASK_PRIO     3
#define IMU_TASK_PRIO     4
#define ENV_PERIOD_MS     1000    /* 1Hz */
#define IMU_PERIOD_MS     40      /* 25Hz — 降低采样率减轻I2C总线+PSRAM带宽压力 */

/* ── UV ADC 配置 ── */
#define UV_ADC_UNIT       ADC_UNIT_1
#define UV_ADC_CHANNEL    ADC_CHANNEL_2   /* GPIO3 = ADC1_CH2 (GPIO4/CH3 被 LCD PIN_DATA0 占用) */

/* ── 全局状态 ── */
static env_snapshot_t   s_env_snap;
static imu_snapshot_t   s_imu_snap;
static SemaphoreHandle_t s_env_mux = NULL;
static SemaphoreHandle_t s_imu_mux = NULL;
static bool s_initialized = false;

/* ── 校准参数持久化 (NVS) ── */
#define NVS_NAMESPACE "sensor_calib"
static bmp280_calib_config_t s_calib_cfg = {
    .pressure_offset_pa = 0.0f,
    .sea_level_pa = 101325.0f,
};

/* 从 NVS 加载校准参数 */
static void calib_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        int32_t p0_raw = 0, offset_raw = 0;
        if (nvs_get_i32(h, "sea_level", &p0_raw) == ESP_OK && p0_raw > 0) {
            s_calib_cfg.sea_level_pa = (float)p0_raw / 10.0f;  /* 存储精度 0.1Pa */
        }
        if (nvs_get_i32(h, "p_offset", &offset_raw) == ESP_OK) {
            s_calib_cfg.pressure_offset_pa = (float)offset_raw / 10.0f;
        }
        nvs_close(h);
        ESP_LOGI(TAG, "NVS 校准参数已加载: P0=%.1f Pa, offset=%+.1f Pa",
                 s_calib_cfg.sea_level_pa, s_calib_cfg.pressure_offset_pa);
    } else {
        ESP_LOGI(TAG, "无 NVS 校准数据，使用默认值");
    }
}

/* 公开接口：提前加载校准参数 (LCD DMA 启动前调用) */
void sensor_manager_preload_calib(void)
{
    calib_load();
}

/* 保存校准参数到 NVS */
static void calib_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "sea_level", (int32_t)(s_calib_cfg.sea_level_pa * 10));
        nvs_set_i32(h, "p_offset", (int32_t)(s_calib_cfg.pressure_offset_pa * 10));
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "校准参数已保存到 NVS: P0=%.1f Pa, offset=%+.1f Pa",
                 s_calib_cfg.sea_level_pa, s_calib_cfg.pressure_offset_pa);
    }
}

/* ─────────────────────────────────────────────────────────
 *  环境传感器任务 (1Hz)
 * ───────────────────────────────────────────────────────── */
static void env_task(void *arg)
{
    ESP_LOGI(TAG, "env_task 启动 @1Hz");

    /* 初始延迟 500ms 等传感器稳定 */
    vTaskDelay(pdMS_TO_TICKS(500));

    while (1) {
        env_snapshot_t snap;
        xSemaphoreTake(s_env_mux, portMAX_DELAY);
        memcpy(&snap, &s_env_snap, sizeof(snap));  /* 基于上次成功快照 */
        xSemaphoreGive(s_env_mux);

        /* 各传感器独立读取，失败不影响其他传感器 */

        /* AHT20 温湿度 */
        if (drv_aht20_read(&snap.aht20) != ESP_OK) {
            ESP_LOGW(TAG, "AHT20 读取失败");
        }

        /* BMP280 气压/海拔 */
        if (drv_bmp280_read(&snap.bmp280) != ESP_OK) {
            ESP_LOGW(TAG, "BMP280 读取失败");
        }

        /* UV */
        if (drv_uv_read(&snap.uv) != ESP_OK) {
            ESP_LOGW(TAG, "UV 读取失败");
        }

        snap.timestamp_us = esp_timer_get_time();

        xSemaphoreTake(s_env_mux, portMAX_DELAY);
        memcpy(&s_env_snap, &snap, sizeof(snap));
        xSemaphoreGive(s_env_mux);

        vTaskDelay(pdMS_TO_TICKS(ENV_PERIOD_MS));
    }
}

/* ─────────────────────────────────────────────────────────
 *  IMU 任务 (50Hz)
 * ───────────────────────────────────────────────────────── */
static void imu_task(void *arg)
{
    ESP_LOGI(TAG, "imu_task 启动 @25Hz");

    /* 初始延迟 200ms 等传感器稳定 */
    vTaskDelay(pdMS_TO_TICKS(200));

    while (1) {
        imu_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));

        /* 1. 读取 MPU-9250/6500（加速度/陀螺仪/温度/AK8963 磁力计） */
        mpu9250_data_t data;
        if (drv_mpu9250_read(&data) == ESP_OK) {
            snap.imu = data;
        } else {
            ESP_LOGW(TAG, "MPU-9250/6500 读取失败");
        }

        /* 2. 优先读取 HMC5883L 独立磁力计
         *    仅在 CONFIG_HMC5883L_ENABLE=y 且驱动初始化成功时读取
         *    避免未挂载模块时频繁 I2C timeout 与 RGB LCD DMA 耦合干扰 */
#ifdef CONFIG_HMC5883L_ENABLE
        if (drv_hmc5883l_is_ready()) {
            if (drv_hmc5883l_read(&snap.compass) != ESP_OK || !snap.compass.valid) {
                ESP_LOGW(TAG, "HMC5883L 读取失败，尝试 QMC5883L 或 AK8963 fallback");
                snap.compass.valid = false;
            }
        }
#endif

        /* 2b. QMC5883L fallback（HMC5883L 未挂载或读取失败时）
         *    QMC5883L 与 HMC5883L 数据结构兼容，将读数填入 compass */
        if (!snap.compass.valid && drv_qmc5883l_is_ready()) {
            if (drv_qmc5883l_read(&snap.qmc) == ESP_OK && snap.qmc.valid) {
                snap.compass.mag[0]  = snap.qmc.mag[0];
                snap.compass.mag[1]  = snap.qmc.mag[1];
                snap.compass.mag[2]  = snap.qmc.mag[2];
                snap.compass.heading = snap.qmc.heading;
                snap.compass.valid   = true;
                snap.compass.source  = HMC5883L_SOURCE_HMC5883L;  /* 外接模块 */
            } else {
                ESP_LOGW(TAG, "QMC5883L 读取失败，使用 AK8963 fallback");
            }
        }

        /* Fallback: HMC5883L 不可用时使用 MPU-9250 内置 AK8963 */
        if (!snap.compass.valid && snap.imu.mag_valid) {
            snap.compass.mag[0] = snap.imu.mag[0];
            snap.compass.mag[1] = snap.imu.mag[1];
            snap.compass.mag[2] = snap.imu.mag[2];
            /* 重新计算方位角 (AK8963 数据沿 X/Y 轴) */
            float heading = atan2f(snap.compass.mag[1], snap.compass.mag[0])
                            * 180.0f / (float)M_PI;
            if (heading < 0) heading += 360.0f;
            snap.compass.heading = heading;
            snap.compass.valid    = true;
            snap.compass.source   = HMC5883L_SOURCE_AK8963;
        }

        snap.timestamp_us = esp_timer_get_time();

        /* 更新最新 IMU 快照 */
        xSemaphoreTake(s_imu_mux, portMAX_DELAY);
        memcpy(&s_imu_snap, &snap, sizeof(snap));
        xSemaphoreGive(s_imu_mux);

        /* 将数据喂给运动分析引擎（更新倾角缓存） */
        if (data.accel[0] != 0 || data.accel[1] != 0 || data.accel[2] != 0) {
            motion_engine_process(&data);
        }

        vTaskDelay(pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
}

/* ── I2C 总线扫描：打印所有应答设备地址 ── */
static void i2c_bus_scan(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "── I2C 总线扫描 ──");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t ret = i2c_master_probe(bus, addr, 50);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  发现设备 @0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "  总线上未发现任何设备！请检查接线。");
    } else {
        ESP_LOGI(TAG, "  共发现 %d 个设备", found);
    }
    ESP_LOGI(TAG, "── 扫描完成 ──");
}

/* ─────────────────────────────────────────────────────────
 *  公开 API
 * ───────────────────────────────────────────────────────── */

esp_err_t sensor_manager_init(i2c_master_bus_handle_t bus)
{
    if (s_initialized) return ESP_OK;
    if (bus == NULL) {
        ESP_LOGE(TAG, "I2C-1 总线句柄为空");
        return ESP_ERR_INVALID_ARG;
    }

    /* ⚠️ i2c_bus_scan 已禁用：i2c_master_probe 在 ESP32-S3 上与 RGB LCD DMA 存在耦合干扰
     * 项目历史已验证会引发黑屏。如需调试可在 menuconfig 中临时开启 CONFIG_SENSOR_I2C_SCAN_DEBUG */
#ifdef CONFIG_SENSOR_I2C_SCAN_DEBUG
    i2c_bus_scan(bus);
#endif

    esp_err_t ret;

    /* NVS 初始化 + 校准参数加载已由 app_main 提前完成
     * (RGB LCD DMA 运行后 spi_flash_read 会触发 cache error) */

    /* 初始化各驱动 (非致命错误，某传感器缺失不影响其他) */
    ret = drv_aht20_init(bus, 0);
    if (ret != ESP_OK) ESP_LOGW(TAG, "AHT20 初始化失败 (跳过)");

    ret = drv_bmp280_init(bus, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BMP280 初始化失败 (跳过) — 尝试备用地址 0x77");
        ret = drv_bmp280_init(bus, 0x77);
        if (ret != ESP_OK) ESP_LOGW(TAG, "BMP280 备用地址也失败 (跳过)");
    }

    /* 应用校准参数到驱动 */
    drv_bmp280_apply_calib(&s_calib_cfg);

    ret = drv_mpu9250_init(bus, 0);
    if (ret != ESP_OK) ESP_LOGW(TAG, "MPU-9250/6500 初始化失败 (跳过)");

    /* 初始化 HMC5883L 独立磁力计 (作为主罗盘数据源，失败不影响其他传感器)
     * 注意：未挂载 HMC5883L 模块时，I2C 读取 timeout 会与 RGB LCD DMA 产生干扰
     * 因此默认禁用，需用户确认模块已接入后在 sdkconfig 设 HMC5883L_ENABLE=1 启用 */
#ifdef CONFIG_HMC5883L_ENABLE
    ret = drv_hmc5883l_init(bus, 0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HMC5883L 磁力计作为主罗盘启用 (优先级 1)");
    } else {
        ESP_LOGW(TAG, "HMC5883L 初始化失败 (尝试 QMC5883L 兼容芯片): %s",
                 esp_err_to_name(ret));
    }
#else
    ESP_LOGI(TAG, "HMC5883L 驱动未启用 (需 sdkconfig 设 CONFIG_HMC5883L_ENABLE=y)");
#endif

    /* ── QMC5883L 初始化（仅在 HMC5883L 未启用或失败时尝试）───────────────
     * QMC5883L 是 HMC5883L 的中国替代品，常记为「HMC5883L」售卖
     * 关键差异：器件地址 0x2C（非 0x1E）、数据顺序 X-Y-Z 小端序
     * 实测识别：启动打 I2C 扫描 @0x2C 但 @0x1E 无应答 → 是 QMC5883L */
    if (!drv_hmc5883l_is_ready()) {
        ret = drv_qmc5883l_init(bus, 0);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "QMC5883L 磁力计作为主罗盘启用 (优先级 2，地址 0x2C)");
        } else {
            ESP_LOGW(TAG, "QMC5883L 初始化失败 (跳过，使用 MPU-9250 AK8963 作为 fallback): %s",
                     esp_err_to_name(ret));
        }
    }

    ret = drv_uv_init(UV_ADC_UNIT, UV_ADC_CHANNEL);
    if (ret != ESP_OK) ESP_LOGW(TAG, "UV ADC 初始化失败 (跳过)");

    /* 创建互斥锁 */
    s_env_mux = xSemaphoreCreateMutex();
    s_imu_mux = xSemaphoreCreateMutex();
    if (!s_env_mux || !s_imu_mux) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }

    /* 启动采样任务 */
    xTaskCreate(env_task, "env_task", ENV_STACK_SIZE, NULL, ENV_TASK_PRIO, NULL);
    xTaskCreate(imu_task, "imu_task", IMU_STACK_SIZE, NULL, IMU_TASK_PRIO, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "传感器管理器初始化完成");
    return ESP_OK;
}

esp_err_t sensor_manager_get_env(env_snapshot_t *out)
{
    if (!s_initialized || !out) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_env_mux, portMAX_DELAY);
    memcpy(out, &s_env_snap, sizeof(*out));
    xSemaphoreGive(s_env_mux);
    return ESP_OK;
}

esp_err_t sensor_manager_get_imu(imu_snapshot_t *out)
{
    if (!s_initialized || !out) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_imu_mux, portMAX_DELAY);
    memcpy(out, &s_imu_snap, sizeof(*out));
    xSemaphoreGive(s_imu_mux);
    return ESP_OK;
}

/* ── 校准接口实现 ── */

esp_err_t sensor_manager_calib_altitude(float known_altitude_m)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* 读取当前气压 */
    env_snapshot_t env;
    if (sensor_manager_get_env(&env) != ESP_OK) {
        ESP_LOGE(TAG, "校准失败: 无法读取传感器数据");
        return ESP_ERR_INVALID_STATE;
    }

    float p0 = drv_bmp280_calc_sea_level(known_altitude_m, env.bmp280.pressure);
    if (p0 <= 0) return ESP_ERR_INVALID_ARG;

    s_calib_cfg.sea_level_pa = p0;
    drv_bmp280_set_sea_level(p0);
    calib_save();
    return ESP_OK;
}

esp_err_t sensor_manager_calib_pressure_offset(float offset_pa)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_calib_cfg.pressure_offset_pa = offset_pa;
    drv_bmp280_set_pressure_offset(offset_pa);
    calib_save();
    return ESP_OK;
}

esp_err_t sensor_manager_calib_reset(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_calib_cfg.sea_level_pa = 101325.0f;
    s_calib_cfg.pressure_offset_pa = 0.0f;
    drv_bmp280_apply_calib(&s_calib_cfg);
    calib_save();
    ESP_LOGI(TAG, "校准参数已重置为默认值");
    return ESP_OK;
}

bmp280_calib_config_t sensor_manager_get_calib(void)
{
    return s_calib_cfg;
}
