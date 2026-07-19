/**
 * @file drv_hmc5883l.h
 * @brief HMC5883L 三轴数字磁阻磁力计 I2C 驱动
 *
 * 基于 Honeywell HMC5883L 数据手册 (HMC5883L_Rev.pdf, 900348 Rev B)
 *
 * 器件特性：
 *   - 量程：±0.88 Ga ~ ±8.1 Ga (默认 ±1.3 Ga, 灵敏度 0.92 mG/LSB)
 *   - 分辨率：12-bit (±1.3 Ga 模式下，2 mG 量级)
 *   - 输出速率：0.75 / 1.5 / 3 / 7.5 / 15 / 30 / 75 Hz
 *   - 接口：I2C 400kHz，固定地址 0x1E (SCL/SDA 引脚直驱)
 *   - 应用：电子罗盘、运动检测、金属检测
 *
 * 寄存器关键点：
 *   - Config A (0x00)：采样平均 (MA) + 输出速率 (DO) + 测量模式 (MS)
 *   - Config B (0x01)：增益 (GN)，决定量程与灵敏度
 *   - Mode     (0x02)：连续测量 / 单次测量 / 空闲
 *   - Data X/Z/Y (0x03~0x08)：6 字节输出（注意顺序为 X-Z-Y 大端序）
 *   - Status    (0x09)：RDY / LOCK
 *   - ID A/B/C  (0x0A/0x0B/0x0C)：固定 'H' '4' '3' = 0x48 0x34 0x33
 *
 * 与 MPU-9250 内置 AK8963 的关系：
 *   - HMC5883L 地址 0x1E，AK8963 地址 0x0C，I2C 总线无冲突
 *   - 推荐 HMC5883L 作为外接独立模块，作为主磁力计使用
 *   - sensor_manager 采用「HMC5883L 优先，AK8963 备用」策略
 */
#ifndef DRV_HMC5883L_H
#define DRV_HMC5883L_H

#include "esp_err.h"
#include "driver/i2c_master.h"

/** 磁力计数据来源标识 (sensor_manager 用于区分主备源) */
typedef enum {
    HMC5883L_SOURCE_NONE = 0,   /**< 无磁力计可用 */
    HMC5883L_SOURCE_HMC5883L,   /**< 使用 HMC5883L 外接模块 */
    HMC5883L_SOURCE_AK8963,     /**< 使用 MPU-9250 内置 AK8963 */
} hmc5883l_source_t;

typedef struct {
    float mag[3];               /**< mx, my, mz 单位 µT（按顺序 X / Y / Z） */
    float heading;              /**< 方位角 0~360°（基于 X/Y 轴 atan2 计算，未做磁偏角补偿） */
    bool  valid;                /**< 数据是否有效 */
    hmc5883l_source_t source;   /**< 本次读数来源 */
} hmc5883l_data_t;

/**
 * @brief 初始化 HMC5883L
 * @param bus  I2C-1 总线句柄
 * @param addr 器件地址 (传 0 使用默认 0x1E)
 *
 * 配置参数：
 *   - 8 采样平均 / 15Hz 输出 / 正常测量模式
 *   - 增益 ±1.3 Ga (灵敏度 0.92 mG/LSB ≈ 92 µT 量程)
 *   - 连续测量模式
 *
 * @return ESP_OK / ESP_ERR_NOT_FOUND (识别失败) / 其他错误码
 */
esp_err_t drv_hmc5883l_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 读取磁力计数据（含方位角计算）
 *        失败时 out->valid=false，其他字段不变
 */
esp_err_t drv_hmc5883l_read(hmc5883l_data_t *out);

/**
 * @brief 查询驱动是否已成功初始化
 */
bool drv_hmc5883l_is_ready(void);

#endif /* DRV_HMC5883L_H */
