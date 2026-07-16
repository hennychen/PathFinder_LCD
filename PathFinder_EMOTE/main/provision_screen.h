/**
 * @file provision_screen.h
 * @brief LVGL 配网 UI 覆盖层 — 4 状态显示
 */
#ifndef PROVISION_SCREEN_H
#define PROVISION_SCREEN_H

#include "lvgl.h"
#include <stdbool.h>

/* ── 配网 UI 状态 (与 wifi_config_manager 对齐) ── */
typedef enum {
    PROV_SCREEN_WAITING = 0,
    PROV_SCREEN_CONNECTING,
    PROV_SCREEN_CONNECTED,
    PROV_SCREEN_FAILED,
} prov_screen_state_t;

/**
 * @brief 创建配网覆盖层 (覆盖在正常 UI 之上)
 *        在 ui_create() 之后调用
 */
void provision_screen_create(lv_obj_t *parent);

/**
 * @brief 更新配网 UI 状态
 * @param state 状态
 * @param ssid 相关 SSID (可为 NULL)
 * @param detail 附加信息如 IP 或错误原因 (可为 NULL)
 */
void provision_screen_set_state(prov_screen_state_t state, const char *ssid, const char *detail);

/**
 * @brief 销毁配网覆盖层, 恢复正常 UI
 */
void provision_screen_destroy(void);

/**
 * @brief 检查配网覆盖层是否在显示
 */
bool provision_screen_is_visible(void);

/**
 * @brief Skip 按钮回调函数类型
 *        用户点击 Skip 后由 provision_screen 内部销毁 UI，
 *        回调中只需处理 Wi-Fi AP 停止等业务逻辑
 */
typedef void (*provision_skip_cb_t)(void);

/**
 * @brief 注册 Skip 按钮回调
 *        必须在 provision_screen_create() 之前调用
 */
void provision_screen_register_skip_cb(provision_skip_cb_t cb);

#endif /* PROVISION_SCREEN_H */
