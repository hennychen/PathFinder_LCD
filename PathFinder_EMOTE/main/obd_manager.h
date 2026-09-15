/**
 * @file obd_manager.h
 * @brief 车辆 OBD-II 数据管理器 — TWAI(CAN) + SN65HVD230 + ISO 15765-4
 *
 * 架构：
 *   obd_task @Core1 → 串行请求-响应轮询标准 Mode 01 PID
 *   RX 由 TWAI ISR 回调投递 FreeRTOS 队列，解析全部在任务侧完成
 *
 * 硬件：
 *   GPIO40 (TWAI TX) → SN65HVD230 CTX/D
 *   GPIO38 (TWAI RX) ← SN65HVD230 CRX/R
 *   CAN 11-bit / 500 kbps（2008 年后车辆 OBD 口强制支持）
 *
 * 线程安全：快照一把互斥锁，与 sensor_manager 同模式
 */
#ifndef OBD_MANAGER_H
#define OBD_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* ── 快照 valid_bits 位定义 ── */
#define OBD_VALID_RPM       (1 << 0)
#define OBD_VALID_SPEED     (1 << 1)
#define OBD_VALID_COOLANT   (1 << 2)
#define OBD_VALID_THROTTLE  (1 << 3)
#define OBD_VALID_BATT      (1 << 4)
#define OBD_VALID_INTAKE    (1 << 5)

/** OBD 数据快照 (各 PID 独立周期更新) */
typedef struct {
    float    rpm;            /**< PID 0x0C: 发动机转速 (256A+B)/4 [rpm] */
    float    speed_kph;      /**< PID 0x0D: 车速 A [km/h] */
    float    coolant_c;      /**< PID 0x05: 冷却液温度 A-40 [°C] */
    float    throttle_pct;   /**< PID 0x11: 节气门开度 A*100/255 [%] */
    float    batt_v;         /**< PID 0x42: 控制模块电压 (256A+B)/1000 [V] */
    float    intake_c;       /**< PID 0x0F: 进气温度 A-40 [°C] */
    uint8_t  valid_bits;     /**< OBD_VALID_* 位组合，收到过有效响应即置位 */
    bool     connected;      /**< 总线在线状态（连续超时后转离线） */
    int64_t  timestamp_us;   /**< 最近一次有效响应时间戳 */
} obd_snapshot_t;

/**
 * @brief 初始化 OBD 管理器
 *        - 创建绑定 Core 1 的 obd_task（TWAI 节点在任务内创建，
 *          使 TWAI 中断注册到 Core 1，与 Core 0 的 LCD DMA 中断隔离）
 *        - 初始化失败仅打日志并保持离线，不影响其他功能
 * @return ESP_OK 或错误码
 */
esp_err_t obd_manager_init(void);

/**
 * @brief 获取最新 OBD 数据快照 (线程安全)
 */
esp_err_t obd_manager_get(obd_snapshot_t *out);

#endif /* OBD_MANAGER_H */
