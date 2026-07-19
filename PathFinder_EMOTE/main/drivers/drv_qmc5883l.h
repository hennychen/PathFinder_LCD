/**
 * @file drv_qmc5883l.h
 * @brief QMC5883L 三轴磁阻磁力计 I2C 驱动
 *
 * 基于 QST QMC5883L 数据手册 (DN0004 v1.4)
 *
 * 与 HMC5883L 的差异：
 *   - 器件地址：0x2C（HMC5883L 是 0x1E），常见于 GY-271 模块
 *   - 数据寄存器从 0x00 开始（HMC5883L 从 0x03 开始）
 *   - 数据顺序 X-Y-Z 小端序（HMC5883L 是 X-Z-Y 大端序）
 *   - 无 ID 寄存器固定字符，使用 0x0D 返回 0xFF 作为识别
 *   - 控制寄存器 0x09，模式连续测量 = 0x01 / 0x09 / 0x1D
 *
 * 器件特性：
 *   - 量程：±2G / ±8G (默认 ±2G，灵敏度 1.5 mG/LSB)
 *   - 输出速率：10 / 50 / 100 / 200 Hz
 *   - 过采样：64 / 128 / 256 / 512
 *   - 接口：I2C 400kHz，固定地址 0x2C
 *
 * 单位换算：
 *   - LSB → 毫高斯 (mG)：raw * 1.5 (±2G 量程)
 *   - mG → 微特斯拉 (µT)：mG * 0.1
 *   - 综合：raw * 0.15 µT/LSB
 *
 * 方位角公式（水平放置时）：
 *   heading = atan2(Y, X) * 180/PI   (转 0~360°)
 */
#ifndef DRV_QMC5883L_H
#define DRV_QMC5883L_H

#include "esp_err.h"
#include "driver/i2c_master.h"

/** 磁力计数据来源标识 */
typedef enum {
    QMC5883L_SOURCE_NONE = 0,
    QMC5883L_SOURCE_QMC5883L,    /**< 外接 QMC5883L 模块 */
} qmc5883l_source_t;

typedef struct {
    float mag[3];               /**< mx, my, mz 单位 µT (X / Y / Z) */
    float heading;              /**< 方位角 0~360°（基于 X/Y atan2，未做磁偏角补偿） */
    bool  valid;                /**< 数据是否有效 */
    qmc5883l_source_t source;   /**< 本次读数来源 */
} qmc5883l_data_t;

/**
 * @brief 初始化 QMC5883L
 * @param bus  I2C 总线句柄
 * @param addr 器件地址（传 0 使用默认 0x2C）
 *
 * 配置：
 *   - 量程 ±2G / 输出速率 50Hz / 过采样 128 / 连续测量
 *   - CTRL=0x41 (OSR=128 / RNG=±2G / ODR=50Hz / MODE=Continuous)
 *
 * @return ESP_OK / ESP_ERR_NOT_FOUND (识别失败) / 其他错误码
 */
esp_err_t drv_qmc5883l_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 读取磁力计数据（含方位角计算）
 */
esp_err_t drv_qmc5883l_read(qmc5883l_data_t *out);

/**
 * @brief 查询驱动是否已成功初始化
 */
bool drv_qmc5883l_is_ready(void);

#endif /* DRV_QMC5883L_H */
