/**
 * @file web_portal.h
 * @brief Web Captive Portal — HTTP Server + Wi-Fi 扫描 + 配网页面
 */
#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include "esp_err.h"

/**
 * @brief 启动 HTTP Server + Captive Portal
 *        在 Wi-Fi AP 模式下调用
 */
esp_err_t web_portal_start(void);

/**
 * @brief 停止 HTTP Server, 释放资源
 *        配网成功后调用
 */
esp_err_t web_portal_stop(void);

/**
 * @brief 检查 HTTP Server 是否在运行
 */
bool web_portal_is_running(void);

#endif /* WEB_PORTAL_H */
