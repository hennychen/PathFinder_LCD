/**
 * @file drv_bmp280.c
 * @brief BMP280 气压/温度/海拔传感器 I2C 驱动实现
 *
 * BMP280 协议：
 *   1. 读取校准系数 (寄存器 0x88~0xA1, 24 字节)
 *   2. 配置: ctrl_meas(0xF4) = 温度×1 + 压力×4 + 正常模式
 *   3. 配置: config(0xF5) = 待机 0.5ms + IIR滤波×4
 *   4. 读取原始数据 (0xF7~0xFC, 6 字节)
 *   5. 应用补偿公式得到真实温度和气压
 *   6. 海拔 = 44330 × (1 − (P/P0)^(1/5.255))
 */
#include "drv_bmp280.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_bmp280";

#define BMP280_ADDR_DEFAULT   0x76
#define BMP280_REG_CALIB      0x88
#define BMP280_REG_ID         0xD0
#define BMP280_REG_RESET      0xE0
#define BMP280_REG_STATUS     0xF3
#define BMP280_REG_CTRL_MEAS  0xF4
#define BMP280_REG_CONFIG     0xF5
#define BMP280_REG_DATA       0xF7
#define BMP280_CHIP_ID        0x58

/* 校准系数 (来自器件内部 OTP) */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp280_calib_t;

static i2c_master_dev_handle_t s_dev   = NULL;
static bmp280_calib_t          s_calib;
static float                   s_t_fine = 0.0f;
static float                   s_sea_level_pa = 101325.0f;

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

/* ── 读取校准系数 ── */
static esp_err_t read_calib(void)
{
    uint8_t c[24];
    esp_err_t ret = read_regs(BMP280_REG_CALIB, c, sizeof(c));
    if (ret != ESP_OK) return ret;

    s_calib.dig_T1 = (uint16_t)((c[1]  << 8) | c[0]);
    s_calib.dig_T2 = (int16_t) ((c[3]  << 8) | c[2]);
    s_calib.dig_T3 = (int16_t) ((c[5]  << 8) | c[4]);
    s_calib.dig_P1 = (uint16_t)((c[7]  << 8) | c[6]);
    s_calib.dig_P2 = (int16_t) ((c[9]  << 8) | c[8]);
    s_calib.dig_P3 = (int16_t) ((c[11] << 8) | c[10]);
    s_calib.dig_P4 = (int16_t) ((c[13] << 8) | c[12]);
    s_calib.dig_P5 = (int16_t) ((c[15] << 8) | c[14]);
    s_calib.dig_P6 = (int16_t) ((c[17] << 8) | c[16]);
    s_calib.dig_P7 = (int16_t) ((c[19] << 8) | c[18]);
    s_calib.dig_P8 = (int16_t) ((c[21] << 8) | c[20]);
    s_calib.dig_P9 = (int16_t) ((c[23] << 8) | c[22]);

    ESP_LOGI(TAG, "校准系数: T1=%u T2=%d T3=%d P1=%u .. P9=%d",
             s_calib.dig_T1, s_calib.dig_T2, s_calib.dig_T3,
             s_calib.dig_P1, s_calib.dig_P9);
    return ESP_OK;
}

/* ── BMP280 官方温度补偿公式 ── */
static float compensate_temperature(int32_t adc_T)
{
    float var1 = ((float)adc_T / 16384.0f - (float)s_calib.dig_T1 / 1024.0f) * (float)s_calib.dig_T2;
    float var2 = (((float)adc_T / 131072.0f - (float)s_calib.dig_T1 / 8192.0f) *
                  ((float)adc_T / 131072.0f - (float)s_calib.dig_T1 / 8192.0f)) * (float)s_calib.dig_T3;
    s_t_fine = var1 + var2;
    return (s_t_fine * 5.0f / 128.0f + 256.0f) / 100.0f;
}

/* ── BMP280 官方气压补偿公式 ── */
static float compensate_pressure(int32_t adc_P)
{
    float var1 = (s_t_fine / 2.0f) - 64000.0f;
    float var2 = var1 * var1 * s_calib.dig_P6 / 32768.0f;
    var2 = var2 + var1 * s_calib.dig_P5 * 2.0f;
    var2 = (var2 / 4.0f) + ((float)s_calib.dig_P4 * 65536.0f);
    var1 = (s_calib.dig_P3 * var1 * var1 / 524288.0f + s_calib.dig_P2 * var1) / 524288.0f;
    var1 = (1.0f + var1 / 32768.0f) * s_calib.dig_P1;
    if (var1 < 0.0000001f) return 0;  /* 避免除零 */

    float p = 1048576.0f - (float)adc_P;
    p = (p - (var2 / 4096.0f)) * 6250.0f / var1;
    var1 = s_calib.dig_P9 * p * p / 2147483648.0f;
    var2 = p * s_calib.dig_P8 / 32768.0f;
    p = p + (var1 + var2 + s_calib.dig_P7) / 16.0f;
    return p;
}

/* ─────────────────────────────────────────────────────────
 *  公开 API
 * ───────────────────────────────────────────────────────── */

void drv_bmp280_set_sea_level(float pa)
{
    if (pa > 0) s_sea_level_pa = pa;
}

esp_err_t drv_bmp280_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (bus == NULL) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (addr == 0) ? BMP280_ADDR_DEFAULT : addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 验证 chip ID */
    uint8_t chip_id = 0;
    ret = read_regs(BMP280_REG_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取 chip ID 失败");
        return ret;
    }
    if (chip_id != BMP280_CHIP_ID) {
        ESP_LOGE(TAG, "芯片 ID 不匹配: 0x%02X (期望 0x%02X)", chip_id, BMP280_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }

    /* 软复位 */
    ret = write_reg(BMP280_REG_RESET, 0xB6);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 读取校准系数 */
    ret = read_calib();
    if (ret != ESP_OK) return ret;

    /* 配置: config(0xF5) = 待机 0.5ms(000) + IIR 滤波×4(010) + SPI关(0)
     *   bit[7:5]=000 standby 0.5ms, bit[4:2]=010 filter coeff 4, bit[0]=0 SPI 3-wire off
     * = 0b00001000 = 0x08 */
    ret = write_reg(BMP280_REG_CONFIG, 0x08);
    if (ret != ESP_OK) return ret;

    /* ctrl_meas(0xF4): osrs_t=01(×1) osrs_p=010(×4) mode=11(normal)
     * = 0b01_010_11 = 0x57 */
    ret = write_reg(BMP280_REG_CTRL_MEAS, 0x57);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "BMP280 初始化完成 @0x%02X", dev_cfg.device_address);
    return ESP_OK;
}

esp_err_t drv_bmp280_read(bmp280_data_t *out)
{
    if (s_dev == NULL || out == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t d[6];
    esp_err_t ret = read_regs(BMP280_REG_DATA, d, sizeof(d));
    if (ret != ESP_OK) return ret;

    int32_t raw_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t raw_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);

    out->temperature = compensate_temperature(raw_T);
    out->pressure    = compensate_pressure(raw_P);

    /* 海拔换算: h = 44330 × (1 − (P/P0)^(1/5.255)) */
    if (out->pressure > 0 && s_sea_level_pa > 0) {
        float ratio = out->pressure / s_sea_level_pa;
        out->altitude = 44330.0f * (1.0f - powf(ratio, 1.0f / 5.255f));
    } else {
        out->altitude = 0;
    }

    return ESP_OK;
}
