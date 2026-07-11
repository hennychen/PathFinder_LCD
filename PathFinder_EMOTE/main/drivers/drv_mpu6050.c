/**
 * @file drv_mpu6050.c
 * @brief MPU-6050 6DOF 加速度计+陀螺仪 I2C 驱动实现
 *
 * MPU-6050 配置：
 *   - 唤醒: PWR_MGMT_1(0x6B) = 0x00
 *   - 加速度量程 ±2g: ACCEL_CONFIG(0x1C) = 0x00
 *   - 陀螺仪量程 ±250°/s: GYRO_CONFIG(0x1B) = 0x00
 *   - 数据从 0x3B 起连续读取 14 字节
 *
 * 换算:
 *   - 加速度: raw / 16384.0 → g (±2g 量程, 16-bit)
 *   - 陀螺仪: raw / 131.0 → °/s (±250°/s 量程, 16-bit)
 *   - 温度: raw / 340.0 + 36.53 → °C
 */
#include "drv_mpu6050.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_mpu6050";

#define MPU6050_ADDR_DEFAULT   0x68

#define REG_SMPLRT_DIV   0x19
#define REG_CONFIG       0x1A
#define REG_GYRO_CONFIG  0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B
#define REG_PWR_MGMT_1   0x6B
#define REG_PWR_MGMT_2   0x6C
#define REG_WHO_AM_I     0x75

/* 量程灵敏度 */
#define ACCEL_LSB_PER_G   16384.0f   /* ±2g */
#define GYRO_LSB_PER_DPS  131.0f     /* ±250°/s */

static i2c_master_dev_handle_t s_dev = NULL;

/* ── 辅助：写单字节寄存器 ── */
static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

/* ── 辅助：读 N 字节寄存器 ── */
static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

esp_err_t drv_mpu6050_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (bus == NULL) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (addr == 0) ? MPU6050_ADDR_DEFAULT : addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 验证 WHO_AM_I */
    uint8_t who = 0;
    ret = read_regs(REG_WHO_AM_I, &who, 1);
    if (ret != ESP_OK || who != 0x68) {
        ESP_LOGE(TAG, "MPU6050 WHO_AM_I = 0x%02X (期望 0x68)", who);
        return ESP_ERR_NOT_FOUND;
    }

    /* 唤醒：复位内部时钟源为 PLL */
    ret = write_reg(REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 再次确认唤醒 (PLL 模式) */
    ret = write_reg(REG_PWR_MGMT_1, 0x01);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 采样率分频器 = 0 → 1kHz / (1+0) = 1kHz */
    ret = write_reg(REG_SMPLRT_DIV, 0x00);
    if (ret != ESP_OK) return ret;

    /* DLPF: 带宽 21Hz → config=0x04 (匹配 25Hz 采样率, 减少混叠) */
    ret = write_reg(REG_CONFIG, 0x04);
    if (ret != ESP_OK) return ret;

    /* 陀螺仪量程 ±250°/s */
    ret = write_reg(REG_GYRO_CONFIG, 0x00);
    if (ret != ESP_OK) return ret;

    /* 加速度量程 ±2g */
    ret = write_reg(REG_ACCEL_CONFIG, 0x00);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "MPU6050 初始化完成 @0x%02X (±2g, ±250°/s, DLPF=21Hz)", dev_cfg.device_address);
    return ESP_OK;
}

esp_err_t drv_mpu6050_read(mpu6050_data_t *out)
{
    if (s_dev == NULL || out == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t d[14];
    esp_err_t ret = read_regs(REG_ACCEL_XOUT_H, d, sizeof(d));
    if (ret != ESP_OK) return ret;

    /* 加速度 X/Y/Z (reg 0x3B~0x40) */
    int16_t ax = (int16_t)((d[0]  << 8) | d[1]);
    int16_t ay = (int16_t)((d[2]  << 8) | d[3]);
    int16_t az = (int16_t)((d[4]  << 8) | d[5]);
    /* 温度 (reg 0x41~0x42) */
    int16_t rt = (int16_t)((d[6]  << 8) | d[7]);
    /* 陀螺仪 X/Y/Z (reg 0x43~0x48) */
    int16_t gx = (int16_t)((d[8]  << 8) | d[9]);
    int16_t gy = (int16_t)((d[10] << 8) | d[11]);
    int16_t gz = (int16_t)((d[12] << 8) | d[13]);

    out->accel[0] = (float)ax / ACCEL_LSB_PER_G;
    out->accel[1] = (float)ay / ACCEL_LSB_PER_G;
    out->accel[2] = (float)az / ACCEL_LSB_PER_G;
    out->gyro[0]  = (float)gx / GYRO_LSB_PER_DPS;
    out->gyro[1]  = (float)gy / GYRO_LSB_PER_DPS;
    out->gyro[2]  = (float)gz / GYRO_LSB_PER_DPS;
    out->temp     = (float)rt / 340.0f + 36.53f;

    return ESP_OK;
}
