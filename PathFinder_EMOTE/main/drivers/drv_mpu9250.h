/**
 * @file drv_mpu9250.h
 * @brief MPU-9250 / MPU-6500 9DOF 加速度计+陀螺仪+磁力计 I2C 驱动
 *
 * 基于 MPU-9250 Product Specification (PS-MPU-9250A-01) 数据手册
 *
 * ── 接线（I2C 模式，对应数据手册 Figure 2(a)）──
 *
 *  MPU-9250 模块      ESP32-S3         说明
 *  ─────────────────────────────────────────
 *  VCC/VDD      →    3V3            (2.4–3.6V)
 *  GND          →    GND
 *  VDDIO        →    3V3            (1.71V–VDD，通常接 3.3V)
 *  SCL/SCLK     →    GPIO12 (SCL)   I2C 时钟线
 *  SDA/SDI      →    GPIO14 (SDA)   I2C 数据线
 *  AD0/SDO      →    GND            → 器件地址 0x68 (接 VDD 为 0x69)
 *  nCS          →    3V3            必须拉高以选择 I2C 模式（否则进入 SPI）
 *  FSYNC        →    GND            不使用帧同步
 *  INT          →    (可选) GPIO    中断输出，不接也可轮询
 *  AUX_CL/AUX_DA→    NC             无外部辅助传感器时不接
 *  REGOUT       →    0.1µF → GND    内部 LDO 滤波电容
 *
 *  外部电容（数据手册 Table 10）：
 *    C1 = 0.1µF (REGOUT → GND)
 *    C2 = 0.1µF (VDD → GND)
 *    C3 = 10nF  (VDDIO → GND)
 *
 * ── 与 MPU-6050 的差异 ──
 *   1. WHO_AM_I 寄存器 (0x75):
 *      MPU-6050 = 0x68, MPU-9250 = 0x71, MPU-6500 = 0x70
 *   2. 温度换算公式不同:
 *      MPU-6050: T = raw/340.0 + 36.53
 *      MPU-9250: T = raw/333.87 + 21.0
 *   3. MPU-9250 内置 AK8963 磁力计 (I2C 地址 0x0C)，需开启 bypass 模式访问
 *   4. I2C 地址与寄存器布局（加速度/陀螺仪/温度）完全兼容
 *
 * 器件地址：0x68 (AD0=GND) 或 0x69 (AD0=VCC)
 * 量程：加速度 ±2g，陀螺仪 ±250°/s，磁力计 ±4800µT (14-bit)
 */
#ifndef DRV_MPU9250_H
#define DRV_MPU9250_H

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    float accel[3];    /**< ax, ay, az 单位 g */
    float gyro[3];     /**< gx, gy, gz 单位 °/s */
    float temp;        /**< 芯片温度 °C */
    float mag[3];      /**< mx, my, mz 单位 µT (仅 MPU-9250 有效，MPU-6500 全零) */
    bool  mag_valid;   /**< 磁力计数据是否有效 */
} mpu9250_data_t;

/**
 * @brief 初始化 MPU-9250 / MPU-6500
 * @param bus  I2C 总线句柄
 * @param addr 器件地址 (传 0 使用默认 0x68)
 * @note 自动检测芯片型号（MPU-9250/MPU-6500），MPU-9250 会额外初始化 AK8963 磁力计
 */
esp_err_t drv_mpu9250_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 读取加速度、陀螺仪、温度、磁力计
 *        MPU-6500 (无磁力计) 的 mag 字段全为 0，mag_valid=false
 */
esp_err_t drv_mpu9250_read(mpu9250_data_t *out);

#endif /* DRV_MPU9250_H */
