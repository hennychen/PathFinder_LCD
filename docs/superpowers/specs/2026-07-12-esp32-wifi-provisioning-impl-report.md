# ESP32 Wi-Fi 双模配网 — 实现报告

> **日期**: 2026-07-12
> **状态**: Implemented & Verified on Hardware
> **设计文档**: `docs/superpowers/specs/2026-07-12-esp32-wifi-provisioning-design.md`

## 1. 实现总览

### 1.1 ESP32 固件侧

| 文件 | 类型 | 职责 |
|------|------|------|
| `wifi_config_manager.c/h` | 新增 | NVS 凭据存取, STA/AP 切换, 重连逻辑, 状态回调 |
| `web_portal.c/h` | 新增 | HTTP Server + DNS 劫持 + Captive Portal + Wi-Fi 扫描 API |
| `provision_screen.c/h` | 新增 | LVGL 配网 UI 覆盖层 (4 状态) |
| `main.c` | 修改 | 集成 wifi_config_manager_init + 状态回调桥梁 |
| `ble_gatt_server.c/h` | 修改 | 新增 C5 (0xFE05) Write+Notify 特征值 |
| `sdkconfig.defaults` | 修改 | Wi-Fi + HTTP Server 配置 |

### 1.2 Flutter App 侧

| 文件 | 类型 | 职责 |
|------|------|------|
| `features/wifi/wifi_setup_screen.dart` | 新增 | Wi-Fi 配网 UI 页面 |
| `core/ble/ble_wifi_writer.dart` | 新增 | BLE C5 Write 封装 (支持 MTU 分片) |
| `core/ble/ble_service_interface.dart` | 修改 | 新增 currentState getter + 3 个 WiFi 抽象方法 |
| `core/ble/ble_uuids.dart` | 修改 | c5RawImuUuid → c5WifiUuid (语义重命名) |
| `core/ble/reactive_ble_service.dart` | 修改 | 实现 3 个 WiFi 方法 |
| `core/ble/mock_ble_service.dart` | 修改 | 实现 WiFi 空方法满足抽象接口 |
| `app/app.dart` | 修改 | AppBar 添加 WiFi 设置入口 |

## 2. 编译阶段修复

### 2.1 ESP-IDF v6.0 事件循环 API 变更

**错误**：`implicit declaration of function 'esp_event_loop_get_handle'`

**原因**：ESP-IDF v6.0 移除了 `esp_event_loop_get_handle()` 函数。

**修复**：直接调用 `esp_event_loop_create_default()`，容忍 `ESP_ERR_INVALID_STATE`（表示已创建）。

```c
// 修复前 (v5.x API)
esp_event_loop_handle_t loop = esp_event_loop_get_handle();
if (loop == NULL) {
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

// 修复后 (v6.0 兼容)
esp_err_t loop_ret = esp_event_loop_create_default();
if (loop_ret != ESP_OK && loop_ret != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(loop_ret);
}
```

**文件**: `wifi_config_manager.c`

### 2.2 LVGL 8.3 进度条指示器颜色 API

**错误**：`implicit declaration of function 'lv_obj_set_style_indic_color'`

**原因**：LVGL 8.3 没有 `lv_obj_set_style_indic_color` 便捷函数。

**修复**：使用 `lv_obj_set_style_bg_color` + `LV_PART_INDICATOR` 选择器。

```c
// 修复前
lv_obj_set_style_indic_color(s_progress_bar, lv_color_hex(COLOR_BLUE), 0);

// 修复后
lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(COLOR_BLUE), LV_PART_INDICATOR);
```

**文件**: `provision_screen.c`

## 3. 真机调试修复 (3 个 Captive Portal 问题)

### 3.1 问题 1：配网页面不弹出（缺少 DNS 劫持）

**现象**：手机可搜到 `PathFinder-EMOTE` 热点，连接后没有自动弹出配网页面。

**根因分析**：

手机 Captive Portal 探测流程：
1. 手机连上热点后，发 DNS 查询解析探测域名
   - Android: `clients3.google.com`
   - iOS: `captive.apple.com`
2. 但 AP 模式下没有 DNS 服务器，域名无法解析
3. 探测请求根本到不了我们的 HTTP Server
4. 手机显示"已连接，无互联网"，不弹配网页面

**修复**：在 `web_portal.c` 中新增 DNS 劫持服务器（UDP 端口 53）：

- 接收所有 DNS 查询，统一返回 `192.168.4.1` 作为 A 记录
- Task 栈大小：3072 bytes
- DNS 响应 TTL：60 秒
- 配网成功后 `close(s_dns_sock)` 自动关闭

**提交**: `e3229b8` "fix: add DNS hijack server for Captive Portal auto-popup on mobile devices"

### 3.2 问题 2：弹出空白页面（302 重定向不被跟随）

**现象**：手机检测到 Captive Portal 后弹出窗口，但显示空白页面。

**根因分析**：

原始设计中探测路径返回 `302 Found` 重定向到 `/`：
```
GET /generate_204 → 302 Found → Location: /
```

但很多手机的 Captive Portal mini-browser **不跟随 302 重定向**，导致空白页面。

**修复**：所有探测路径直接返回配网页面 HTML 内容：

```c
// 修复前
static esp_err_t handler_redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
}

// 修复后
static esp_err_t handler_captive(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

**提交**: `8acad59` "fix: captive portal probes return HTML directly instead of 302 redirect"

### 3.3 问题 3：Header Field are too long

**现象**：手机浏览器访问配网页面时返回 HTTP 错误 "Header Field are too long"。

**根因分析**：

ESP-IDF HTTP Server 默认请求头限制 `CONFIG_HTTPD_MAX_REQ_HDR_LEN=512` 字节。手机浏览器的 `User-Agent`、`Accept`、`Accept-Encoding` 等头部轻松超过 512 字节，请求被拒绝。

**修复**：

```ini
# sdkconfig.defaults
# 修复前
CONFIG_HTTPD_MAX_REQ_HDR_LEN=512
CONFIG_HTTPD_MAX_URI_LEN=512

# 修复后
CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
CONFIG_HTTPD_MAX_URI_LEN=1024
```

**提交**: `84a7ea5` "fix: increase HTTPD header length to 2048 to fix 'Header Field too long' on mobile browsers"

## 4. Captive Portal 完整工作流

```
手机连接 PathFinder-EMOTE 热点
        │
        ▼
┌───────────────────────────┐
│ DHCP 分配 IP + DNS 地址    │
│ (DNS = 192.168.4.1)       │
└───────────┬───────────────┘
            │
            ▼
┌───────────────────────────┐
│ DNS 查询 (UDP 53)          │
│ clients3.google.com →     │
│   返回 192.168.4.1        │
└───────────┬───────────────┘
            │
            ▼
┌───────────────────────────┐
│ HTTP 探测 (端口 80)        │
│ GET /generate_204 →       │
│   直接返回配网页面 HTML    │
└───────────┬───────────────┘
            │
            ▼
┌───────────────────────────┐
│ 手机弹出 Captive Portal    │
│ 显示 WiFi 扫描列表         │
│ 用户选择 SSID + 输入密码   │
│ POST /api/connect          │
└───────────┬───────────────┘
            │
            ▼
┌───────────────────────────┐
│ wifi_config_manager        │
│ NVS 保存凭据               │
│ STA 连接路由器             │
│ 成功 → 销毁 AP+HTTP+DNS   │
└───────────────────────────┘
```

## 5. Git 提交历史

| 提交 | 描述 |
|------|------|
| `07a3c5d` | fix: resolve ESP-IDF v6.0 event loop API and LVGL 8.3 indic color compilation errors |
| `e3229b8` | fix: add DNS hijack server for Captive Portal auto-popup on mobile devices |
| `8acad59` | fix: captive portal probes return HTML directly instead of 302 redirect |
| `84a7ea5` | fix: increase HTTPD header length to 2048 to fix 'Header Field too long' on mobile browsers |

## 6. 验证状态

| 测试项 | 状态 |
|--------|------|
| 固件编译 (ESP-IDF v6.0) | ✅ 通过 |
| 固件烧录 | ✅ 成功 |
| Flutter APK 构建 | ✅ 25.2MB |
| ESP32 启动 → AP 模式 | ✅ 串口日志确认 |
| DNS 劫持启动 | ✅ "DNS 劫持已启动" 日志 |
| HTTP Server 启动 | ✅ "Web Portal 已启动" 日志 |
| 手机热点检测 | ✅ 可搜到 PathFinder-EMOTE |
| Captive Portal 弹窗 | ✅ 自动弹出 |
| 配网页面显示 | ✅ WiFi 列表正常 |
| Flutter BLE 配网 | ⏳ 需真机测试 |
| 配网成功后内存释放 | ⏳ 需验证 |
