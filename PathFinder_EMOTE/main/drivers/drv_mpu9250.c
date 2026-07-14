/**
 * @file drv_mpu9250.c
 * @brief MPU-9250 / MPU-6500 9DOF I2C 驱动实现
 *
 * 基于 MPU-9250 Product Specification (PS-MPU-9250A-01) 数据手册
 *
 * 支持芯片:
 *   - MPU-9250: 加速度+陀螺仪+AK8963磁力计 (WHO_AM_I = 0x71)
 *   - MPU-6500: 加速度+陀螺仪 (WHO_AM_I = 0x70)
 *
 * 寄存器布局与 MPU-6050 完全兼容（加速度/陀螺仪/温度），差异点:
 *   1. WHO_AM_I 值不同 (0x71 / 0x70 vs 0x68)
 *   2. 温度换算: T = raw / 333.87 + 21.0 (vs raw/340 + 36.53)
 *   3. MPU-9250 内置 AK8963 磁力计，需开启 I2C bypass 模式访问
 *
 * AK8963 磁力计访问方式:
 *   - 设置 MPU-9250 INT_PIN_CFG(0x37) bit1 = 1 开启 bypass
 *   - 此时 AK8963 (I2C 地址 0x0C) 直接挂到主 I2C 总线上
 *   - 通过 bus 句柄添加 AK8963 子设备，独立读写
 */
#include "drv_mpu9250.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_mpu9250";

/* ── MPU-9250/6500 主器件地址 ── */
#define MPU9250_ADDR_DEFAULT    0x68

/* ── MPU-9250 寄存器 ── */
#define REG_SMPLRT_DIV          0x19
#define REG_CONFIG              0x1A
#define REG_GYRO_CONFIG         0x1B
#define REG_ACCEL_CONFIG        0x1C
#define REG_ACCEL_CONFIG2       0x1D
#define REG_INT_PIN_CFG         0x37
#define REG_ACCEL_XOUT_H        0x3B
#define REG_PWR_MGMT_1          0x6B
#define REG_PWR_MGMT_2          0x6C
#define REG_WHO_AM_I            0x75

/* ── AK8963 磁力计 (I2C 地址 0x0C) ── */
#define AK8963_ADDR             0x0C
#define AK8963_WHO_AM_I         0x00    /* 返回 0x48 */
#define AK8963_HXL              0x03    /* 磁力计数据起始地址 (6字节) */
#define AK8963_ST2              0x09    /* 状态寄存器2 (读取后清除溢出标志) */
#define AK8963_CNTL1            0x0A    /* 控制寄存器1 */
#define AK8963_CNTL2            0x0B    /* 控制寄存器2 (软复位) */
#define AK8963_ASA_X            0x10    /* 灵敏度补偿值 X */
#define AK8963_ASA_Y            0x11    /* 灵敏度补偿值 Y */
#define AK8963_ASA_Z            0x12    /* 灵敏度补偿值 Z */

/* ── 量程灵敏度 ── */
#define ACCEL_LSB_PER_G         16384.0f   /* ±2g */
#define GYRO_LSB_PER_DPS        131.0f     /* ±250°/s */
#define MAG_LSB_PER_UT          0.6f       /* 14-bit, ±4800µT */

/* ── 芯片类型 ── */
typedef enum {
    CHIP_MPU9250 = 0,
    CHIP_MPU6500 = 1,
    CHIP_UNKNOWN = 2,
} chip_type_t;

/* ── 全局状态 ── */
static i2c_master_dev_handle_t s_dev = NULL;       /* MPU-9250 主器件 */
static i2c_master_dev_handle_t s_mag_dev = NULL;   /* AK8963 磁力计 (仅 MPU-9250) */
static chip_type_t s_chip = CHIP_UNKNOWN;
static bool s_has_mag = false;

/* AK8963 灵敏度补偿系数 */
static float s_asa_x = 1.0f, s_asa_y = 1.0f, s_asa_z = 1.0f;

/* ── 辅助：写 MPU 单字节寄存器 ── */
static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

/* ── 辅助：读 MPU N 字节寄存器 ── */
static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

/* ── 辅助：写 AK8963 单字节寄存器 ── */
static esp_err_t mag_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_mag_dev, buf, 2, 100);
}

/* ── 辅助：读 AK8963 N 字节寄存器 ── */
static esp_err_t mag_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_mag_dev, &reg, 1, buf, len, 100);
}

/* ── 初始化 AK8963 磁力计 ── */
static esp_err_t ak8963_init(void)
{
    esp_err_t ret;

    /* 验证 AK8963 WHO_AM_I */
    uint8_t who = 0;
    ret = mag_read_regs(AK8963_WHO_AM_I, &who, 1);
    if (ret != ESP_OK || who != 0x48) {
        ESP_LOGW(TAG, "AK8963 WHO_AM_I = 0x%02X (期望 0x48)，磁力计不可用", who);
        return ESP_ERR_NOT_FOUND;
    }

    /* 读取灵敏度补偿值 (ASA) */
    uint8_t asa[3];
    ret = mag_read_regs(AK8963_ASA_X, asa, 3);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AK8963 ASA 读取失败，使用默认补偿");
    } else {
        s_asa_x = ((float)asa[0] - 128.0f) / 256.0f + 1.0f;
        s_asa_y = ((float)asa[1] - 128.0f) / 256.0f + 1.0f;
        s_asa_z = ((float)asa[2] - 128.0f) / 256.0f + 1.0f;
        ESP_LOGI(TAG, "AK8963 ASA 补偿系数: X=%.3f Y=%.3f Z=%.3f", s_asa_x, s_asa_y, s_asa_z);
    }

    /* 软复位 AK8963 */
    ret = mag_write_reg(AK8963_CNTL2, 0x01);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 设置连续测量模式2 (100Hz, 16-bit) */
    ret = mag_write_reg(AK8963_CNTL1, 0x16);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "AK8963 磁力计初始化完成 (100Hz, 16-bit)");
    return ESP_OK;
}

esp_err_t drv_mpu9250_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (bus == NULL) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (addr == 0) ? MPU9250_ADDR_DEFAULT : addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 验证 WHO_AM_I — 兼容 MPU-9250 (0x71) 和 MPU-6500 (0x70) */
    uint8_t who = 0;
    ret = read_regs(REG_WHO_AM_I, &who, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I 读取失败: %s", esp_err_to_name(ret));
        return ret;
    }

    if (who == 0x71) {
        s_chip = CHIP_MPU9250;
        ESP_LOGI(TAG, "检测到 MPU-9250 @0x%02X (WHO_AM_I=0x71)", dev_cfg.device_address);
    } else if (who == 0x70) {
        /* 某些 MPU-9250 模块内部芯片返回 0x70 (MPU-6500 ID)，但仍含 AK8963 磁力计
         * 统一视为 MPU-9250 并尝试初始化磁力计 */
        s_chip = CHIP_MPU9250;
        ESP_LOGI(TAG, "检测到 MPU-9250/6500 @0x%02X (WHO_AM_I=0x70)，将尝试磁力计", dev_cfg.device_address);
    } else {
        ESP_LOGE(TAG, "未知芯片 WHO_AM_I = 0x%02X (期望 MPU-9250=0x71 或 MPU-6500=0x70)", who);
        return ESP_ERR_NOT_FOUND;
    }

    /* 唤醒：复位内部时钟源为 PLL */
    ret = write_reg(REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 再次确认唤醒 (PLL 模式, 时钟源=陀螺仪X轴) */
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

    /* 加速度 DLPF 带宽 21Hz */
    ret = write_reg(REG_ACCEL_CONFIG2, 0x04);
    if (ret != ESP_OK) return ret;

    /* ── 尝试初始化 AK8963 磁力计 (MPU-9250 含磁力计) ── */
    ESP_LOGI(TAG, "开始尝试初始化 AK8963 磁力计...");

    /* 开启 I2C bypass 模式，使 AK8963 直接挂到主 I2C 总线 */
    ret = write_reg(REG_INT_PIN_CFG, 0x02);  /* BYPASS_EN */
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C bypass 开启失败 (err=%s)，磁力计不可用", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "I2C bypass 模式已开启");
        vTaskDelay(pdMS_TO_TICKS(50));  /* 增加延迟等待 bypass 生效 */

        /* 验证 bypass 是否生效：读取 INT_PIN_CFG 寄存器 */
        uint8_t bypass_val = 0;
        ret = read_regs(REG_INT_PIN_CFG, &bypass_val, 1);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "INT_PIN_CFG (0x37) = 0x%02X (BYPASS_EN bit1 = %d)",
                     bypass_val, (bypass_val >> 1) & 0x01);
        }

        /* 添加 AK8963 子设备到 I2C 总线 */
        i2c_device_config_t mag_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = AK8963_ADDR,
            .scl_speed_hz    = 400000,
        };
        ret = i2c_master_bus_add_device(bus, &mag_cfg, &s_mag_dev);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "AK8963 I2C 设备添加失败: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "AK8963 I2C 设备已添加 @0x%02X", AK8963_ADDR);
            ret = ak8963_init();
            if (ret == ESP_OK) {
                s_has_mag = true;
                ESP_LOGI(TAG, "AK8963 磁力计初始化成功");
            } else {
                ESP_LOGW(TAG, "AK8963 初始化失败 (err=%s)，磁力计不可用", esp_err_to_name(ret));
                s_has_mag = false;
            }
        }
    }

    ESP_LOGI(TAG, "MPU-9250/6500 初始化完成 @0x%02X (±2g, ±250°/s, DLPF=21Hz, mag=%s)",
             dev_cfg.device_address, s_has_mag ? "ON" : "OFF");
    return ESP_OK;
}

/* ── 读取 AK8963 磁力计数据 ── */
static esp_err_t ak8963_read(float mag_out[3])
{
    if (!s_has_mag || s_mag_dev == NULL) {
        mag_out[0] = mag_out[1] = mag_out[2] = 0.0f;
        return ESP_ERR_INVALID_STATE;
    }

    /* 检查数据就绪 (ST1 bit0 = DRDY) */
    uint8_t st1 = 0;
    esp_err_t ret = mag_read_regs(0x02, &st1, 1);
    if (ret != ESP_OK) return ret;
    if (!(st1 & 0x01)) {
        /* 数据未就绪，返回上次缓存值（此处简化返回零） */
        mag_out[0] = mag_out[1] = mag_out[2] = 0.0f;
        return ESP_OK;
    }

    /* 读取 6 字节磁力计数据 (HXL~HZH) + ST2 */
    uint8_t d[7];
    ret = mag_read_regs(AK8963_HXL, d, 7);
    if (ret != ESP_OK) return ret;

    /* 检查溢出标志 (ST2 bit3 = HOFL) */
    if (d[6] & 0x08) {
        ESP_LOGW(TAG, "AK8963 磁传感器溢出");
        mag_out[0] = mag_out[1] = mag_out[2] = 0.0f;
        return ESP_OK;
    }

    /* AK8963 数据为小端序 (低字节在前)，与 MPU 寄存器大端序不同 */
    int16_t mx = (int16_t)((d[1] << 8) | d[0]);
    int16_t my = (int16_t)((d[3] << 8) | d[2]);
    int16_t mz = (int16_t)((d[5] << 8) | d[4]);

    /* 应用灵敏度补偿并转换为 µT */
    mag_out[0] = (float)mx * MAG_LSB_PER_UT * s_asa_x;
    mag_out[1] = (float)my * MAG_LSB_PER_UT * s_asa_y;
    mag_out[2] = (float)mz * MAG_LSB_PER_UT * s_asa_z;

    return ESP_OK;
}

esp_err_t drv_mpu9250_read(mpu9250_data_t *out)
{
    if (s_dev == NULL || out == NULL) return ESP_ERR_INVALID_STATE;

    /* 读取加速度+温度+陀螺仪 (14字节, 0x3B~0x48) */
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
    /* MPU-9250/6500 温度换算 (与 MPU-6050 不同) */
    out->temp     = (float)rt / 333.87f + 21.0f;

    /* 磁力计 (仅 MPU-9250 有效) */
    out->mag_valid = s_has_mag;
    if (s_has_mag) {
        ak8963_read(out->mag);
    } else {
        out->mag[0] = out->mag[1] = out->mag[2] = 0.0f;
    }

    return ESP_OK;
}
