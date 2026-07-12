/**
 * @file ble_gatt_server.h
 * @brief BLE GATT Server — 向手机 App 广播传感器数据
 *
 * 服务 UUID: 0000fe00-0000-1000-8000-00805f9b34fb
 * 特征值:
 *   C2 (fe02) 环境数据   20 bytes  notify  @1Hz
 *   C3 (fe03) 运动数据   8 bytes   notify  @10Hz
 *   C4 (fe04) 表情状态   15 bytes  notify  on-change
 *
 * 二进制协议与 Flutter EnvSnapshot/ImuSnapshot/EmoteInfo.fromBle() 对齐
 */
#ifndef BLE_GATT_SERVER_H
#define BLE_GATT_SERVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化 BLE GATT 服务端并开始广播
 *        内部创建 NimBLE host 任务，注册 GATT 服务
 */
esp_err_t ble_gatt_server_init(void);

/* ── 数据推送接口（在传感器任务中调用）── */

/**
 * @brief 更新环境数据并通知订阅的客户端
 * @param temp_x100   温度 × 100 (°C)
 * @param humi_x100   湿度 × 100 (%)
 * @param pressure_pa 气压 (Pa)
 * @param alt_x10     海拔 × 10 (m)
 * @param uv_x100     UV指数 × 100
 */
void ble_gatt_notify_env(int16_t temp_x100, uint16_t humi_x100,
                         uint32_t pressure_pa, int16_t alt_x10,
                         uint16_t uv_x100);

/**
 * @brief 更新运动数据并通知订阅的客户端
 * @param pitch_x100   俯仰角 × 100 (°)
 * @param roll_x100    横滚角 × 100 (°)
 * @param accel_x1000  合加速度偏差 × 1000 (g)
 * @param event        运动事件码 (0-12)
 * @param confidence   置信度 0-100
 */
void ble_gatt_notify_motion(int16_t pitch_x100, int16_t roll_x100,
                            uint16_t accel_x1000, uint8_t event,
                            uint8_t confidence);

/**
 * @brief 更新表情状态并通知订阅的客户端
 * @param emote_id   表情 ID
 * @param name       表情名称 (ASCII, 最多 12 字符)
 * @param trigger    触发原因码
 */
void ble_gatt_notify_emote(uint8_t emote_id, const char *name,
                           uint8_t trigger);

/**
 * @brief 检查是否有客户端连接
 */
bool ble_gatt_is_connected(void);

/* ── C5 WiFi 配网特征值 (Write + Notify) ── */

/**
 * @brief 向连接的客户端发送配网状态 JSON (通过 C5 Notify)
 * @param json_str JSON 字符串 (UTF-8)
 */
void ble_gatt_notify_wifi_status(const char *json_str);

/**
 * @brief 注册 C5 Write 回调 (收到 App 发来的 JSON 命令时调用)
 * @param cb 回调函数, 参数为收到的 JSON 字符串
 */
typedef void (*ble_wifi_write_cb_t)(const char *json_str);
void ble_gatt_register_wifi_write_cb(ble_wifi_write_cb_t cb);

#endif /* BLE_GATT_SERVER_H */
