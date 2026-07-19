/**
 * @file drv_hmc5883l.c
 * @brief HMC5883L 三轴数字磁阻磁力计 I2C 驱动实现
 *
 * 基于 Honeywell HMC5883L 数据手册 (900348 Rev B) 实现要点：
 *   1. WHO_AM_I 校验：读取 0x0A~0x0C 三字节，必须为 'H' '4' '3'
 *   2. Config A (0x00) = 0x70 → MA=8x / DO=15Hz / MS=Normal
 *   3. Config B (0x01) = 0x20 → GN=001 → ±1.3 Ga, 灵敏度 0.92 mG/LSB
 *   4. Mode     (0x02) = 0x00 → 连续测量模式
 *
 * 数据寄存器读取顺序 (重要)：
 *   0x03 X MSB → 0x04 X LSB → 0x05 Z MSB → 0x06 Z LSB → 0x07 Y MSB → 0x08 Y LSB
 *   （注意：HMC5883L 物理排布为 X-Z-Y，与常见 X-Y-Z 顺序不同，读取时需注意）
 *
 * 单位换算：
 *   - LSB → 毫高斯 (mG)：raw * 0.92 (±1.3 Ga 增益)
 *   - mG → 微特斯拉 (µT)：mG * 0.1  (1 G = 100 µT, 1 mG = 0.1 µT)
 *   - 综合：raw * 0.092 µT/LSB
 *
 * 方位角公式（水平放置时）：
 *   heading = atan2(Y, X) * 180/PI   (转 0~360°)
 */
#include "drv_hmc5883l.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_hmc5883l";

/* ── HMC5883L 主器件地址 (7-bit) ── */
#define HMC5883L_ADDR_DEFAULT    0x1E

/* ── HMC5883L 寄存器 ── */
#define REG_CONFIG_A             0x00
#define REG_CONFIG_B             0x01
#define REG_MODE                 0x02
#define REG_DATA_X_MSB           0x03   /* 数据起始 (6 字节, X-Z-Y 顺序) */
#define REG_STATUS               0x09
#define REG_ID_A                 0x0A   /* 固定标识 'H' = 0x48 */
#define REG_ID_B                 0x0B   /* 固定标识 '4' = 0x34 */
#define REG_ID_C                 0x0C   /* 固定标识 '3' = 0x33 */

/* ── 增益 / 灵敏度常量 ── */
/* GN=001 → ±1.3 Ga，灵敏度 0.92 mG/LSB → 0.092 µT/LSB */
#define MAG_LSB_PER_UT           0.092f

/* ── 全局状态 ── */
static i2c_master_dev_handle_t s_dev    = NULL;
static bool                     s_ready  = false;

/* ── 辅助：写单字节寄存器 ── */
static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

/* ── 辅助：读 N 字节寄存器 ── */
static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

esp_err_t drv_hmc5883l_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (bus == NULL) return ESP_ERR_INVALID_ARG;
    if (s_ready) return ESP_OK;  /* 已初始化 */

    /* 添加 HMC5883L 到 I2C-1 总线 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (addr == 0) ? HMC5883L_ADDR_DEFAULT : addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── 验证 ID 寄存器 ('H' '4' '3') ── */
    uint8_t id[3] = {0};
    ret = read_regs(REG_ID_A, id, 3);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HMC5883L ID 读取失败: %s", esp_err_to_name(ret));
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    if (id[0] != 0x48 || id[1] != 0x34 || id[2] != 0x33) {
        ESP_LOGW(TAG, "HMC5883L ID 不匹配: %02X %02X %02X (期望 48 34 33)", id[0], id[1], id[2]);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "检测到 HMC5883L @0x%02X (ID='H43')", dev_cfg.device_address);

    /* ── Config A: 8 采样平均 + 15Hz 输出 + 正常测量 ──
     * MA=01 (8x)  << 5  → 0x40
     * DO=100 (15Hz) << 2 → 0x10
     * MD=00 (Normal)    → 0x00
     * 组合: 0b 0111 0000 = 0x70 */
    ret = write_reg(REG_CONFIG_A, 0x70);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Config A 写入失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── Config B: 增益 ±1.3 Ga (GN=001) ──
     * GN=001 << 5 → 0b 0010 0000 = 0x20
     * 灵敏度 0.92 mG/LSB ≈ 92 µT 满量程 */
    ret = write_reg(REG_CONFIG_B, 0x20);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Config B 写入失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── Mode: 连续测量 (MD=00) ── */
    ret = write_reg(REG_MODE, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mode 写入失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 等待首次测量完成 (15Hz ≈ 67ms，留余量至 100ms) */
    vTaskDelay(pdMS_TO_TICKS(100));

    s_ready = true;
    ESP_LOGI(TAG, "HMC5883L 初始化完成 (±1.3 Ga, 15Hz, 8x 平均, 连续模式)");
    return ESP_OK;
}

esp_err_t drv_hmc5883l_read(hmc5883l_data_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ready || s_dev == NULL) {
        out->valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    /* 读取 6 字节数据 (X-Z-Y 顺序，大端序) */
    uint8_t d[6];
    esp_err_t ret = read_regs(REG_DATA_X_MSB, d, sizeof(d));
    if (ret != ESP_OK) {
        out->valid = false;
        return ret;
    }

    /* HMC5883L 数据为大端序：[MSB, LSB] 拼接为 int16 */
    int16_t rx = (int16_t)(((uint16_t)d[0] << 8) | d[1]);   /* X */
    int16_t rz = (int16_t)(((uint16_t)d[2] << 8) | d[3]);   /* Z (注意顺序) */
    int16_t ry = (int16_t)(((uint16_t)d[4] << 8) | d[5]);   /* Y */

    /* LSB → µT (0.92 mG/LSB × 0.1 µT/mG = 0.092 µT/LSB) */
    out->mag[0] = (float)rx * MAG_LSB_PER_UT;
    out->mag[1] = (float)ry * MAG_LSB_PER_UT;
    out->mag[2] = (float)rz * MAG_LSB_PER_UT;

    /* 方位角 (atan2 返回 -PI~+PI, 转 0~360°) */
    float heading = atan2f(out->mag[1], out->mag[0]) * 180.0f / (float)M_PI;
    if (heading < 0) heading += 360.0f;
    out->heading = heading;

    out->valid  = true;
    out->source = HMC5883L_SOURCE_HMC5883L;
    return ESP_OK;
}

bool drv_hmc5883l_is_ready(void)
{
    return s_ready;
}
