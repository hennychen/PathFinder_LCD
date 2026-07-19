/**
 * @file drv_qmc5883l.c
 * @brief QMC5883L 三轴磁阻磁力计 I2C 驱动实现
 *
 * 基于 QST QMC5883L 数据手册 (DN0004 v1.4)
 *
 * 关键差异（对比 HMC5883L）：
 *   1. 器件地址 0x2C（HMC5883L 是 0x1E）
 *   2. 数据寄存器从 0x00 起（HMC5883L 是 0x03 起）
 *   3. 数据顺序 X-Y-Z 小端序（HMC5883L 是 X-Z-Y 大端序）
 *   4. 控制寄存器 0x09（HMC5883L 是 0x00/0x01/0x02）
 *   5. 0x0D 寄存器返回固定 0xFF 作为识别依据
 *
 * CTRL (0x09) 位定义：
 *   bit 7-6: ODR  (输出速率)  00=10Hz / 01=50Hz / 10=100Hz / 11=200Hz
 *   bit 5-4: RNG  (量程)      00=±2G  / 01=±8G
 *   bit 3-2: OSR  (过采样)    00=64   / 01=128 / 10=256 / 11=512
 *   bit 1-0: MODE (模式)      00=Standby / 01=Continuous
 *
 * 配置 0x41 = 0100 0001:
 *   ODR=01 (50Hz) / RNG=00 (±2G) / OSR=01 (128) / MODE=01 (Continuous)
 */
#include "drv_qmc5883l.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_qmc5883l";

/* ── QMC5883L 主器件地址 (7-bit) ── */
#define QMC5883L_ADDR_DEFAULT    0x2C

/* ── QMC5883L 寄存器 ──
 * 注意：实测该模块寄存器布局与官方 datasheet 有差异
 *   - 0x00: 固定标志 (读出始终是 0x80)
 *   - 0x01-0x06: 数据 X_LSB/X_MSB/Y_LSB/Y_MSB/Z_LSB/Z_MSB (小端序)
 *   - 0x07: STATUS (DRDY bit0)
 *   - 0x09: CTRL (实测写不进 0x45，芯片默认 0x18 已是测量模式)
 *   - 0x0A: PERIOD
 *   - 0x0D: CHIP_ID (实测返回 0x00，非官方 0xFF) */
#define REG_DATA_START          0x01   /* X_LSB 起始 (不是 0x00!) */
#define REG_STATUS              0x07   /* 状态：bit0=DRDY */
#define REG_TEMP_LSB             0x08   /* 温度寄存器（可选） */
#define REG_CTRL                 0x09   /* 控制寄存器 */
#define REG_PERIOD               0x0A   /* PERIOD 寄存器 */
#define REG_CHIP_ID              0x0D   /* ID 寄存器 (实测返回 0x00) */

/* ── 增益 / 灵敏度常量 ──
 * ±2G 量程下灵敏度 = 1.5 mG/LSB
 * 换算到 µT: 1.5 mG × 0.1 µT/mG = 0.15 µT/LSB */
#define MAG_LSB_PER_UT           0.15f

/* CTRL 寄存器组合值 */
#define CTRL_OSR_128             (0x01 << 2)
#define CTRL_RNG_2G              (0x00 << 4)
#define CTRL_ODR_50HZ            (0x01 << 6)
#define CTRL_MODE_CONTINUOUS     (0x01)
#define CTRL_CONFIG              (CTRL_ODR_50HZ | CTRL_RNG_2G | CTRL_OSR_128 | CTRL_MODE_CONTINUOUS)

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

esp_err_t drv_qmc5883l_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (bus == NULL) return ESP_ERR_INVALID_ARG;
    if (s_ready) return ESP_OK;  /* 已初始化 */

    /* 添加 QMC5883L 到 I2C 总线 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (addr == 0) ? QMC5883L_ADDR_DEFAULT : addr,
        .scl_speed_hz    = 100000,  /* 共用触摸总线，统一 100kHz */
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── 验证芯片 ID 寄存器 (0x0D 应返回 0xFF) ──
     * 注意：某些 QMC5883L clone 可能返回 0xFF/0x01/其他值，放宽校验
     * 只在完全无应答（NACK）时报错 */
    uint8_t chip_id = 0;
    ret = read_regs(REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "QMC5883L chip ID 读取失败: %s", esp_err_to_name(ret));
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "检测到 QMC5883L @0x%02X (chip_id=0x%02X)", dev_cfg.device_address, chip_id);

    /* ── 完整扫描 0x00~0x0F 寄存器布局 ── */
    {
        uint8_t regs[16] = {0};
        for (uint8_t r = 0; r < 16; r++) {
            uint8_t v = 0xFF;
            esp_err_t e = read_regs(r, &v, 1);
            regs[r] = v;
            (void)e;
        }
        ESP_LOGI(TAG, "寄存器扫描 0x00-0x0F: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7],
                 regs[8], regs[9], regs[10], regs[11], regs[12], regs[13], regs[14], regs[15]);
    }


    /* ── (1) 软复位：CTRL=0x80 ── */
    ret = write_reg(REG_CTRL, 0x80);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "软复位写入失败: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* ── (2) 设置 PERIOD 寄存器=0x01 (官方推荐，不设测量不出数据) ──
     * 某些 QMC5883L clone 在不写 PERIOD 的情况下会保持 STATUS=0x00 且数据全为 0
     * 同时试 0x09 是官方 sample code 的值，这里两个都试 */
    write_reg(REG_PERIOD, 0x01);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* ── (3) 配置 CTRL: OSR=128 / RNG=±2G / ODR=50Hz / Continuous ── */
    ret = write_reg(REG_CTRL, CTRL_CONFIG);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CTRL 配置写入失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── (4) 验证 CTRL 是否真的写入成功 + 全量寄存器扫描 ── */
    uint8_t ctrl_readback = 0;
    read_regs(REG_CTRL, &ctrl_readback, 1);
    uint8_t period_readback = 0;
    read_regs(REG_PERIOD, &period_readback, 1);
    ESP_LOGI(TAG, "CTRL 回读: 0x%02X (期望 0x%02X) | PERIOD 回读: 0x%02X (期望 0x01)",
             ctrl_readback, CTRL_CONFIG, period_readback);

    /* 再次扫描 0x00-0x0F 寄存器看变化 */
    {
        uint8_t regs[16] = {0};
        for (uint8_t r = 0; r < 16; r++) {
            uint8_t v = 0xFF;
            read_regs(r, &v, 1);
            regs[r] = v;
        }
        ESP_LOGI(TAG, "配置后扫描 0x00-0x0F: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7],
                 regs[8], regs[9], regs[10], regs[11], regs[12], regs[13], regs[14], regs[15]);
    }

    /* ── (5) 等待首次测量完成（按2秒保证跨足多个采样周期） ── */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* ── (6) 读取 STATUS + 首批数据验证 ── */
    uint8_t status = 0;
    read_regs(REG_STATUS, &status, 1);
    uint8_t data_peek[6] = {0};
    read_regs(REG_DATA_START, data_peek, 6);
    int16_t vx = (int16_t)(((uint16_t)data_peek[1] << 8) | data_peek[0]);
    int16_t vy = (int16_t)(((uint16_t)data_peek[3] << 8) | data_peek[2]);
    int16_t vz = (int16_t)(((uint16_t)data_peek[5] << 8) | data_peek[4]);
    ESP_LOGI(TAG, "启动后 STATUS=0x%02X DRDY=%d | raw=[%02X %02X %02X %02X %02X %02X] X=%d Y=%d Z=%d",
             status, status & 0x01,
             data_peek[0], data_peek[1], data_peek[2],
             data_peek[3], data_peek[4], data_peek[5],
             vx, vy, vz);

    s_ready = true;
    ESP_LOGI(TAG, "QMC5883L 初始化完成 (±2G, 50Hz, OSR=128, 连续模式, CTRL=0x%02X)",
             CTRL_CONFIG);
    return ESP_OK;
}

esp_err_t drv_qmc5883l_read(qmc5883l_data_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ready || s_dev == NULL) {
        out->valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    /* 读取 6 字节数据 (X-Y-Z 顺序，小端序)
     * 寄存器 0x01~0x06: XL XH YL YH ZL ZH
     * 注意：该模块数据从 0x01 起读（0x00 是固定标志 0x80） */
    uint8_t d[6] = {0};
    uint8_t status = 0;
    esp_err_t ret = read_regs(REG_DATA_START, d, sizeof(d));
    if (ret != ESP_OK) {
        out->valid = false;
        return ret;
    }
    read_regs(REG_STATUS, &status, 1);

    /* 数据为小端序：[LSB, MSB] 拼接为 int16 */
    int16_t rx = (int16_t)(((uint16_t)d[1] << 8) | d[0]);   /* X */
    int16_t ry = (int16_t)(((uint16_t)d[3] << 8) | d[2]);   /* Y */
    int16_t rz = (int16_t)(((uint16_t)d[5] << 8) | d[4]);   /* Z */

    /* LSB → µT (1.5 mG/LSB × 0.1 µT/mG = 0.15 µT/LSB) */
    out->mag[0] = (float)rx * MAG_LSB_PER_UT;
    out->mag[1] = (float)ry * MAG_LSB_PER_UT;
    out->mag[2] = (float)rz * MAG_LSB_PER_UT;

    /* 方位角 (atan2 返回 -PI~+PI, 转 0~360°) */
    float heading = atan2f(out->mag[1], out->mag[0]) * 180.0f / (float)M_PI;
    if (heading < 0) heading += 360.0f;
    out->heading = heading;

    out->valid  = true;
    out->source = QMC5883L_SOURCE_QMC5883L;

    /* 调试输出（每5秒一次）：解析后的 µT 值与方位角 */
    static int64_t last_dbg_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (now_us - last_dbg_us > 5000000) {
        last_dbg_us = now_us;
        ESP_LOGI(TAG, "X=%.1f Y=%.1f Z=%.1f µT | heading=%.0f°",
                 out->mag[0], out->mag[1], out->mag[2], out->heading);
    }
    return ESP_OK;
}

bool drv_qmc5883l_is_ready(void)
{
    return s_ready;
}
