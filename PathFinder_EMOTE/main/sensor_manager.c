/**
 * @file sensor_manager.c
 * @brief 传感器管理器实现
 */
#include "sensor_manager.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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
#define UV_ADC_CHANNEL    ADC_CHANNEL_3   /* GPIO3 = ADC1_CH3 */

/* ── 全局状态 ── */
static env_snapshot_t   s_env_snap;
static imu_snapshot_t   s_imu_snap;
static SemaphoreHandle_t s_env_mux = NULL;
static SemaphoreHandle_t s_imu_mux = NULL;
static bool s_initialized = false;

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
        memset(&snap, 0, sizeof(snap));
        bool ok = true;

        /* AHT20 温湿度 */
        if (drv_aht20_read(&snap.aht20) != ESP_OK) {
            ESP_LOGW(TAG, "AHT20 读取失败");
            ok = false;
        }

        /* BMP280 气压/海拔 */
        if (drv_bmp280_read(&snap.bmp280) != ESP_OK) {
            ESP_LOGW(TAG, "BMP280 读取失败");
            ok = false;
        }

        /* UV */
        if (drv_uv_read(&snap.uv) != ESP_OK) {
            ESP_LOGW(TAG, "UV 读取失败");
            ok = false;
        }

        snap.timestamp_us = esp_timer_get_time();

        if (ok) {
            xSemaphoreTake(s_env_mux, portMAX_DELAY);
            memcpy(&s_env_snap, &snap, sizeof(snap));
            xSemaphoreGive(s_env_mux);
        }

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
        mpu6050_data_t data;
        if (drv_mpu6050_read(&data) == ESP_OK) {
            imu_snapshot_t snap;
            snap.imu = data;
            snap.timestamp_us = esp_timer_get_time();

            /* 更新最新 IMU 快照 */
            xSemaphoreTake(s_imu_mux, portMAX_DELAY);
            memcpy(&s_imu_snap, &snap, sizeof(snap));
            xSemaphoreGive(s_imu_mux);

            /* 将数据喂给运动分析引擎（更新倾角缓存） */
            motion_engine_process(&data);
        }

        vTaskDelay(pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
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

    esp_err_t ret;

    /* 初始化各驱动 (非致命错误，某传感器缺失不影响其他) */
    ret = drv_aht20_init(bus, 0);
    if (ret != ESP_OK) ESP_LOGW(TAG, "AHT20 初始化失败 (跳过)");

    ret = drv_bmp280_init(bus, 0);
    if (ret != ESP_OK) ESP_LOGW(TAG, "BMP280 初始化失败 (跳过)");

    ret = drv_mpu6050_init(bus, 0);
    if (ret != ESP_OK) ESP_LOGW(TAG, "MPU6050 初始化失败 (跳过)");

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
