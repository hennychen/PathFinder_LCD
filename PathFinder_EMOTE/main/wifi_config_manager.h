/**
 * @file wifi_config_manager.h
 * @brief Wi-Fi 配置管理器 — NVS 存取, STA/AP 切换, 重连逻辑
 */
#ifndef WIFI_CONFIG_MANAGER_H
#define WIFI_CONFIG_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

/* ── 配网状态 ── */
typedef enum {
    WIFI_PROV_STATE_IDLE = 0,       /* 空闲 */
    WIFI_PROV_STATE_PROVISIONING,   /* 配网模式 (AP+BLE 就绪) */
    WIFI_PROV_STATE_CONNECTING,     /* 正在连接 STA */
    WIFI_PROV_STATE_CONNECTED,      /* STA 已连接 */
    WIFI_PROV_STATE_FAILED,         /* 连接失败 */
} wifi_prov_state_t;

/* ── 状态回调 ── */
typedef void (*wifi_prov_state_cb_t)(wifi_prov_state_t state, const char *ssid, const char *detail);

/* ── 初始化 ── */
/**
 * @brief 初始化 Wi-Fi 配置管理器
 *        读取 NVS，有凭据则启动 STA，无凭据则启动配网模式
 *        必须在 NVS init 之后、BLE init 之前调用
 */
esp_err_t wifi_config_manager_init(void);

/* ── 配网操作 (BLE / Web Portal 调用) ── */

/**
 * @brief 设置 Wi-Fi 凭据并尝试连接
 *        保存到 NVS，切换 STA 模式尝试连接
 * @param ssid Wi-Fi SSID (最长 32 字节)
 * @param password Wi-Fi 密码 (最长 64 字节)
 */
esp_err_t wifi_config_manager_set_credentials(const char *ssid, const char *password);

/**
 * @brief 清除 NVS 中的 Wi-Fi 凭据，重启进入配网模式
 */
esp_err_t wifi_config_manager_reset(void);

/**
 * @brief 获取当前配网状态
 */
wifi_prov_state_t wifi_config_manager_get_state(void);

/**
 * @brief 检查是否已连接 Wi-Fi
 */
bool wifi_config_manager_is_connected(void);

/**
 * @brief 获取已连接的 SSID
 */
const char *wifi_config_manager_get_ssid(void);

/**
 * @brief 获取已分配的 IP 地址 (STA 模式)
 */
const char *wifi_config_manager_get_ip(void);

/**
 * @brief 注册状态变化回调 (供 LVGL / BLE Notify 调用)
 */
void wifi_config_manager_register_cb(wifi_prov_state_cb_t cb);

/**
 * @brief 跳过配网：停止 AP 模式与 Web Portal，进入 IDLE 状态
 *        不清除已有凭据，不重启。用户可通过 reset_wifi 重新配网。
 */
esp_err_t wifi_config_manager_skip_provisioning(void);

/**
 * @brief 获取已存储的路由器 SSID（供 Mesh 根节点读取）
 * @return SSID 字符串指针，未配网时返回空字符串
 */
const char *wifi_config_manager_get_router_ssid(void);

/**
 * @brief 获取已存储的路由器密码（供 Mesh 根节点读取）
 * @return 密码字符串指针，未配网时返回空字符串
 */
const char *wifi_config_manager_get_router_pass(void);

#endif /* WIFI_CONFIG_MANAGER_H */
