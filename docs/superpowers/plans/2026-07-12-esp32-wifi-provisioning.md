# ESP32 Wi-Fi 双模配网实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 PathFinder EMOTE 设备增加 BLE + Web 双模 Wi-Fi 配网能力，配网成功后 AP+HTTP 立即销毁释放内存。

**Architecture:** 新增 `wifi_config_manager` 核心模块统一管理 NVS 凭据存取与 STA/AP 切换，`web_portal` 提供 Captive Portal HTTP 端点，`provision_screen` 提供 LVGL 4 状态配网 UI，BLE 侧新增 C5 (0xFE05) Write+Notify 特征值接收 JSON 命令。

**Tech Stack:** ESP-IDF (esp_wifi, esp_http_server, NimBLE), LVGL 8.3, Flutter (flutter_reactive_ble, Riverpod)

**Spec:** `docs/superpowers/specs/2026-07-12-esp32-wifi-provisioning-design.md`

---

## 文件结构

### ESP32 固件侧 (PathFinder_EMOTE/main/)

**新增：**
- `wifi_config_manager.h/c` — NVS 凭据存取，STA/AP 模式切换，重连逻辑，状态事件回调
- `web_portal.h/c` — HTTP Server，Captive Portal，Wi-Fi 扫描 API，配网页面 HTML
- `provision_screen.h/c` — LVGL 配网 UI 覆盖层 (WAITING/CONNECTING/CONNECTED/FAILED)

**修改：**
- `main.c` — app_main() 加入 wifi_config_manager_init()
- `ble_gatt_server.h/c` — 新增 C5 (0xFE05) Write+Notify 特征值
- `CMakeLists.txt` — SRCS + REQUIRES 更新
- `sdkconfig.defaults` — Wi-Fi + HTTP Server 配置

### Flutter App 侧 (PathFinder_Dashboard/lib/)

**新增：**
- `features/wifi/wifi_setup_screen.dart` — Wi-Fi 配网页面
- `features/wifi/widgets/wifi_scan_list.dart` — 扫描列表组件
- `core/ble/ble_wifi_writer.dart` — BLE C5 Write 封装

**修改：**
- `core/ble/ble_uuids.dart` — 重命名 c5RawImuUuid → c5WifiUuid
- `core/ble/reactive_ble_service.dart` — 加 writeWifiConfig() 方法
- `app/app.dart` — 加 Wi-Fi 设置入口

---

## Task 1: sdkconfig + CMakeLists 构建配置

**Files:**
- Modify: `PathFinder_EMOTE/sdkconfig.defaults`
- Modify: `PathFinder_EMOTE/main/CMakeLists.txt`

- [ ] **Step 1: 更新 sdkconfig.defaults**

在文件末尾追加 Wi-Fi 和 HTTP Server 配置：

```ini

# Wi-Fi
CONFIG_ESP_WIFI_ENABLED=y
CONFIG_ESP_WIFI_NVS_ENABLED=y
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y

# HTTP Server (用于 Web Portal)
CONFIG_HTTPD_MAX_REQ_HDR_LEN=512
CONFIG_HTTPD_MAX_URI_LEN=512
```

- [ ] **Step 2: 更新 CMakeLists.txt**

将 SRCS 列表添加三个新源文件，REQUIRES 列表添加三个新依赖：

```cmake
idf_component_register(
    SRCS
        "main.c"
        "LCD.c"
        "app_emote_assets.c"
        "drivers/drv_aht20.c"
        "drivers/drv_bmp280.c"
        "drivers/drv_mpu6050.c"
        "drivers/drv_uv_adc.c"
        "sensor_manager.c"
        "motion_engine.c"
        "emote_engine.c"
        "ble_gatt_server.c"
        "wifi_config_manager.c"
        "web_portal.c"
        "provision_screen.c"
    INCLUDE_DIRS
        "."
        "drivers"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_adc
        esp_lcd
        esp_timer
        nvs_flash
        bt
        esp_wifi
        esp_http_server
        esp_event
        esp_netif
)
```

- [ ] **Step 3: 验证构建**

```bash
cd PathFinder_EMOTE
idf.py build
```

Expected: 构建失败（因为三个新 .c 文件还不存在），但 sdkconfig 和 CMakeLists 解析应无错误。

- [ ] **Step 4: Commit**

```bash
git add PathFinder_EMOTE/sdkconfig.defaults PathFinder_EMOTE/main/CMakeLists.txt
git commit -m "build: add Wi-Fi, HTTP Server, event deps to sdkconfig and CMakeLists"
```

---

## Task 2: wifi_config_manager 模块

**Files:**
- Create: `PathFinder_EMOTE/main/wifi_config_manager.h`
- Create: `PathFinder_EMOTE/main/wifi_config_manager.c`

- [ ] **Step 1: 编写 wifi_config_manager.h**

```c
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

#endif /* WIFI_CONFIG_MANAGER_H */
```

- [ ] **Step 2: 编写 wifi_config_manager.c**

```c
/**
 * @file wifi_config_manager.c
 * @brief Wi-Fi 配置管理器实现
 */
#include "wifi_config_manager.h"
#include "web_portal.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

#define NVS_NAMESPACE        "wifi_cred"
#define NVS_KEY_SSID         "ssid"
#define NVS_KEY_PASS         "pass"
#define MAX_SSID_LEN         32
#define MAX_PASS_LEN         64
#define CONNECT_TIMEOUT_MS   10000
#define MAX_RETRY_COUNT      5

/* ── 内部状态 ── */
static wifi_prov_state_t s_state = WIFI_PROV_STATE_IDLE;
static char s_ssid[MAX_SSID_LEN + 1] = {0};
static char s_pass[MAX_PASS_LEN + 1] = {0};
static char s_ip_str[16] = {0};
static int  s_retry_count = 0;
static bool s_wifi_started = false;
static wifi_prov_state_cb_t s_state_cb = NULL;
static EventGroupHandle_t s_wifi_events = NULL;

#define BIT_CONNECTED   BIT0
#define BIT_FAILED      BIT1

/* ── 设置状态并通知回调 ── */
static void set_state(wifi_prov_state_t new_state, const char *detail)
{
    s_state = new_state;
    ESP_LOGI(TAG, "状态: %d ssid='%s' detail='%s'", new_state, s_ssid, detail ? detail : "");
    if (s_state_cb) {
        s_state_cb(new_state, s_ssid, detail ? detail : "");
    }
}

/* ── Wi-Fi 事件处理 ── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA 已启动, 开始连接...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            s_retry_count++;
            ESP_LOGW(TAG, "STA 断开, 重试 %d/%d", s_retry_count, MAX_RETRY_COUNT);

            if (s_retry_count >= MAX_RETRY_COUNT) {
                ESP_LOGE(TAG, "重试次数耗尽, 重进配网模式");
                xEventGroupSetBits(s_wifi_events, BIT_FAILED);
                /* 清除凭据, 防止下次又用错误凭据 */
                wifi_config_manager_reset();
            } else {
                set_state(WIFI_PROV_STATE_CONNECTING, "retrying");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_wifi_connect();
            }
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "AP: 设备已连接");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "AP: 设备已断开");
            break;

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "获取 IP: %s", s_ip_str);
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, BIT_CONNECTED);
        set_state(WIFI_PROV_STATE_CONNECTED, s_ip_str);

        /* 连接成功后销毁 Web Portal */
        web_portal_stop();
    }
}

/* ── 从 NVS 读取凭据 ── */
static bool load_credentials_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t ssid_len = sizeof(s_ssid);
    size_t pass_len = sizeof(s_pass);
    err = nvs_get_str(handle, NVS_KEY_SSID, s_ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }
    err = nvs_get_str(handle, NVS_KEY_PASS, s_pass, &pass_len);
    nvs_close(handle);

    if (err != ESP_OK) {
        return false;
    }

    ESP_LOGI(TAG, "从 NVS 读取到 Wi-Fi: SSID='%s'", s_ssid);
    return true;
}

/* ── 保存凭据到 NVS ── */
static esp_err_t save_credentials_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(handle, NVS_KEY_SSID, ssid);
    nvs_set_str(handle, NVS_KEY_PASS, password);
    nvs_commit(handle);
    nvs_close(handle);

    strncpy(s_ssid, ssid, MAX_SSID_LEN);
    s_ssid[MAX_SSID_LEN] = '\0';
    strncpy(s_pass, password, MAX_PASS_LEN);
    s_pass[MAX_PASS_LEN] = '\0';

    ESP_LOGI(TAG, "凭据已保存到 NVS: SSID='%s'", ssid);
    return ESP_OK;
}

/* ── 启动配网模式 (AP + Web Portal) ── */
static void start_provisioning(void)
{
    ESP_LOGI(TAG, "启动配网模式: AP + Web Portal");

    /* 设置 APSTA 模式 */
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    /* 配置 AP */
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "PathFinder-EMOTE",
            .ssid_len = strlen("PathFinder-EMOTE"),
            .channel = 1,
            .password = "",
            .max_connection = 1,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    if (!s_wifi_started) {
        esp_wifi_start();
        s_wifi_started = true;
    }

    set_state(WIFI_PROV_STATE_PROVISIONING, "AP ready");

    /* 启动 Web Portal */
    web_portal_start();
}

/* ── 尝试 STA 连接 ── */
static void try_sta_connect(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "尝试 STA 连接: SSID='%s'", ssid);

    /* 如果 AP 模式在运行, 先停 AP + Web Portal */
    if (s_state == WIFI_PROV_STATE_PROVISIONING) {
        web_portal_stop();
    }

    /* 切换到 STA 模式 */
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, MAX_SSID_LEN);
    strncpy((char *)sta_config.sta.password, password, MAX_PASS_LEN);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);

    if (!s_wifi_started) {
        esp_wifi_start();
        s_wifi_started = true;
    }

    s_retry_count = 0;
    set_state(WIFI_PROV_STATE_CONNECTING, ssid);

    /* 等待连接结果 */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, BIT_CONNECTED | BIT_FAILED,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

    if (bits & BIT_CONNECTED) {
        xEventGroupClearBits(s_wifi_events, BIT_CONNECTED);
        ESP_LOGI(TAG, "STA 连接成功");
    } else {
        xEventGroupClearBits(s_wifi_events, BIT_FAILED);
        ESP_LOGE(TAG, "STA 连接超时");
        set_state(WIFI_PROV_STATE_FAILED, "timeout");
    }
}

/* ════════════════════════════════════════════════
 *  公共 API
 * ════════════════════════════════════════════════ */

esp_err_t wifi_config_manager_init(void)
{
    ESP_LOGI(TAG, "初始化 Wi-Fi 配置管理器");

    /* 创建事件组 */
    s_wifi_events = xEventGroupCreate();

    /* 初始化网络接口 */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 创建默认事件循环 (如果还没有) */
    esp_event_loop_handle_t loop = esp_event_loop_get_handle();
    if (loop == NULL) {
        ESP_ERROR_CHECK(esp_event_loop_create_default());
    }

    /* 创建 STA 和 AP netif */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    /* Wi-Fi 初始化 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册事件处理 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* 尝试从 NVS 读取凭据 */
    if (load_credentials_from_nvs()) {
        /* 有凭据 → STA 模式 */
        ESP_LOGI(TAG, "有凭据, 启动 STA 连接");
        try_sta_connect(s_ssid, s_pass);
    } else {
        /* 无凭据 → 配网模式 */
        ESP_LOGI(TAG, "无凭据, 启动配网模式");
        start_provisioning();
    }

    return ESP_OK;
}

esp_err_t wifi_config_manager_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > MAX_SSID_LEN) {
        ESP_LOGE(TAG, "SSID 无效");
        return ESP_ERR_INVALID_ARG;
    }
    if (!password || strlen(password) > MAX_PASS_LEN) {
        password = "";
    }

    ESP_LOGI(TAG, "收到配网请求: SSID='%s'", ssid);

    /* 保存凭据 */
    esp_err_t err = save_credentials_to_nvs(ssid, password);
    if (err != ESP_OK) return err;

    /* 尝试连接 */
    try_sta_connect(ssid, password);

    return ESP_OK;
}

esp_err_t wifi_config_manager_reset(void)
{
    ESP_LOGI(TAG, "清除 Wi-Fi 凭据");

    /* 清除 NVS */
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, NVS_KEY_SSID);
        nvs_erase_key(handle, NVS_KEY_PASS);
        nvs_commit(handle);
        nvs_close(handle);
    }

    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_pass, 0, sizeof(s_pass));
    memset(s_ip_str, 0, sizeof(s_ip_str));

    /* 重启进入配网模式 */
    esp_restart();
    return ESP_OK;
}

wifi_prov_state_t wifi_config_manager_get_state(void)
{
    return s_state;
}

bool wifi_config_manager_is_connected(void)
{
    return s_state == WIFI_PROV_STATE_CONNECTED;
}

const char *wifi_config_manager_get_ssid(void)
{
    return s_ssid;
}

const char *wifi_config_manager_get_ip(void)
{
    return s_ip_str;
}

void wifi_config_manager_register_cb(wifi_prov_state_cb_t cb)
{
    s_state_cb = cb;
}
```

- [ ] **Step 3: 验证编译**

```bash
cd PathFinder_EMOTE
idf.py build
```

Expected: 编译失败（缺少 web_portal.h），但 wifi_config_manager.c 本身语法正确。

- [ ] **Step 4: Commit**

```bash
git add PathFinder_EMOTE/main/wifi_config_manager.h PathFinder_EMOTE/main/wifi_config_manager.c
git commit -m "feat: add wifi_config_manager with NVS persistence and STA/AP switching"
```

---

## Task 3: web_portal 模块

**Files:**
- Create: `PathFinder_EMOTE/main/web_portal.h`
- Create: `PathFinder_EMOTE/main/web_portal.c`

- [ ] **Step 1: 编写 web_portal.h**

```c
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
```

- [ ] **Step 2: 编写 web_portal.c**

```c
/**
 * @file web_portal.c
 * @brief Web Captive Portal 实现
 */
#include "web_portal.h"
#include "wifi_config_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "web_portal";

static httpd_handle_t s_server = NULL;

/* ── 配网页面 HTML (Flash RODATA, 不占 RAM) ── */
static const char PROGMEM PORTAL_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>PathFinder WiFi Setup</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui;background:#0d1117;color:#c9d1d9;padding:20px}"
"h2{color:#00B4FF;text-align:center;margin:20px 0}"
".card{background:#161b22;border-radius:12px;padding:16px;margin:12px 0}"
".ssid-item{display:flex;justify-content:space-between;align-items:center;"
"padding:12px;border-bottom:1px solid #30363d;cursor:pointer}"
".ssid-item:hover{background:#21262d}"
".ssid-name{font-size:16px;color:#fff}"
".rssi{font-size:12px;color:#8b949e}"
"input[type='password']{width:100%;padding:12px;margin:8px 0;border-radius:8px;"
"border:1px solid #30363d;background:#0d1117;color:#fff;font-size:16px}"
"button{width:100%;padding:14px;border:none;border-radius:8px;"
"background:#00B4FF;color:#000;font-size:16px;font-weight:bold;cursor:pointer;margin-top:8px}"
"button:disabled{opacity:0.5}"
"#status{text-align:center;margin:12px 0;font-size:14px}"
".loading{text-align:center;color:#8b949e;padding:20px}"
"</style></head><body>"
"<h2>📶 PathFinder WiFi</h2>"
"<div id='status'></div>"
"<div id='scan' class='loading'>Scanning WiFi networks...</div>"
"<div id='form' style='display:none'>"
"<div class='card'>"
"<input type='password' id='pass' placeholder='WiFi Password'>"
"<button id='btn' onclick='connect()'>Connect</button>"
"</div></div>"
"<script>"
"let ssid='';"
"fetch('/api/scan').then(r=>r.json()).then(d=>{"
"let h='<div class=\\'card\\'>';"
"d.forEach(n=>{h+='<div class=\\'ssid-item\\' onclick=\\'pick(\\\"'+n.ssid+'\\\")\\'>"
"<span class=\\'ssid-name\\'>'+n.ssid+'</span>"
"<span class=\\'rssi\\'>'+n.rssi+' dBm</span></div>'});"
"document.getElementById('scan').innerHTML=h+'</div>';});"
"function pick(s){ssid=s;document.getElementById('scan').style.display='none';"
"document.getElementById('form').style.display='block';"
"document.getElementById('status').innerHTML='Selected: <b>'+s+'</b>';}"
"function connect(){"
"let p=document.getElementById('pass').value;"
"document.getElementById('btn').disabled=true;"
"document.getElementById('status').innerHTML='Connecting...';"
"fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:ssid,password:p})}).then(r=>r.json()).then(d=>{"
"if(d.status==='ok'){poll();}else{document.getElementById('status').innerHTML='Error: '+d.msg;"
"document.getElementById('btn').disabled=false;}});}"
"function poll(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"if(d.state==='connected'){document.getElementById('status').innerHTML="
"'✅ Connected to '+d.ssid+' ('+d.ip+')';}"
"else if(d.state==='failed'){document.getElementById('status').innerHTML="
"'❌ Failed: '+d.detail;document.getElementById('btn').disabled=false;}"
"else{setTimeout(poll,1000);}});}"
"</script></body></html>";

/* ── GET / — 返回配网页面 ── */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ── GET /api/scan — 返回 Wi-Fi 扫描结果 JSON ── */
static esp_err_t handler_scan(httpd_req_t *req)
{
    wifi_scan_config_t scan_cfg = {0};
    esp_wifi_scan_start(&scan_cfg, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t aps[20];
    esp_wifi_scan_get_ap_records(&ap_count, aps);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char *)aps[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(item, "auth", aps[i].authmode);
        cJSON_AddItemToArray(root, item);
    }

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── POST /api/connect — 接收凭据 ── */
static esp_err_t handler_connect(httpd_req_t *req)
{
    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_OK;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(root, "password");

    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_OK;
    }

    const char *ssid = ssid_item->valuestring;
    const char *pass = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";

    /* 异步设置凭据 (避免阻塞 HTTP 响应) */
    wifi_config_manager_set_credentials(ssid, pass);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    const char *resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)resp_str);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── GET /api/status — 返回当前状态 ── */
static esp_err_t handler_status(httpd_req_t *req)
{
    cJSON *resp = cJSON_CreateObject();
    const char *state_str = "unknown";
    switch (wifi_config_manager_get_state()) {
        case WIFI_PROV_STATE_PROVISIONING: state_str = "provisioning"; break;
        case WIFI_PROV_STATE_CONNECTING:   state_str = "connecting"; break;
        case WIFI_PROV_STATE_CONNECTED:    state_str = "connected"; break;
        case WIFI_PROV_STATE_FAILED:       state_str = "failed"; break;
        default: state_str = "idle"; break;
    }
    cJSON_AddStringToObject(resp, "state", state_str);
    cJSON_AddStringToObject(resp, "ssid", wifi_config_manager_get_ssid());
    cJSON_AddStringToObject(resp, "ip", wifi_config_manager_get_ip());
    if (wifi_config_manager_get_state() == WIFI_PROV_STATE_FAILED) {
        cJSON_AddStringToObject(resp, "detail", "auth_failed");
    }

    const char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

/* ── Captive Portal 重定向处理 ── */
static esp_err_t handler_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
}

/* ════════════════════════════════════════════════
 *  公共 API
 * ════════════════════════════════════════════════ */

esp_err_t web_portal_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "Web Portal 已在运行");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP Server 启动失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 注册 URI 处理器 */
    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = handler_root };
    httpd_uri_t uri_scan = { .uri = "/api/scan", .method = HTTP_GET, .handler = handler_scan };
    httpd_uri_t uri_connect = { .uri = "/api/connect", .method = HTTP_POST, .handler = handler_connect };
    httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = handler_status };

    /* Captive Portal 探测路径 */
    httpd_uri_t uri_gen204 = { .uri = "/generate_204", .method = HTTP_GET, .handler = handler_redirect };
    httpd_uri_t uri_hotspot = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handler_redirect };
    httpd_uri_t uri_ncsi = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = handler_redirect };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_connect);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_gen204);
    httpd_register_uri_handler(s_server, &uri_hotspot);
    httpd_register_uri_handler(s_server, &uri_ncsi);

    ESP_LOGI(TAG, "Web Portal 已启动 (端口 80)");
    return ESP_OK;
}

esp_err_t web_portal_stop(void)
{
    if (!s_server) return ESP_OK;

    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Web Portal 已停止, 内存已释放");
    return ESP_OK;
}

bool web_portal_is_running(void)
{
    return s_server != NULL;
}
```

- [ ] **Step 3: 验证编译**

```bash
cd PathFinder_EMOTE
idf.py build
```

Expected: 编译失败（缺少 provision_screen.c），但 wifi_config_manager.c + web_portal.c 本身语法正确。

- [ ] **Step 4: Commit**

```bash
git add PathFinder_EMOTE/main/web_portal.h PathFinder_EMOTE/main/web_portal.c
git commit -m "feat: add web_portal with Captive Portal, WiFi scan API, and connect endpoint"
```

---

## Task 4: provision_screen LVGL 模块

**Files:**
- Create: `PathFinder_EMOTE/main/provision_screen.h`
- Create: `PathFinder_EMOTE/main/provision_screen.c`

- [ ] **Step 1: 编写 provision_screen.h**

```c
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

#endif /* PROVISION_SCREEN_H */
```

- [ ] **Step 2: 编写 provision_screen.c**

```c
/**
 * @file provision_screen.c
 * @brief LVGL 配网 UI 覆盖层实现
 *        复用 EMA 胶囊风格 (#00B4FF 蓝 / #00FF88 绿 / #FFB400 黄 / #FF5050 红)
 */
#include "provision_screen.h"
#include "esp_timer.h"

/* ── 颜色常量 (与 main.c EMA 胶囊色系一致) ── */
#define COLOR_BLUE    0x00B4FF
#define COLOR_GREEN   0x00FF88
#define COLOR_YELLOW  0xFFB400
#define COLOR_RED     0xFF5050
#define COLOR_WHITE   0xFFFFFF
#define COLOR_GREY    0x8b949e

static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_detail_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_status_capsule = NULL;
static lv_obj_t *s_status_capsule_lbl = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_pulse_dots[3] = {NULL};

/* 脉冲动画 */
static lv_anim_t s_pulse_anim;
static bool s_pulse_active = false;

static void pulse_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
}

static void pulse_start(void)
{
    if (s_pulse_active) return;
    for (int i = 0; i < 3; i++) {
        if (s_pulse_dots[i]) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_pulse_dots[i]);
            lv_anim_set_values(&a, 100, 255);
            lv_anim_set_time(&a, 800);
            lv_anim_set_playback_time(&a, 800);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_exec_cb(&a, pulse_cb);
            lv_anim_set_delay(&a, i * 200);
            lv_anim_start(&a);
        }
    }
    s_pulse_active = true;
}

static void pulse_stop(void)
{
    if (!s_pulse_active) return;
    for (int i = 0; i < 3; i++) {
        if (s_pulse_dots[i]) {
            lv_anim_del(s_pulse_dots[i], pulse_cb);
        }
    }
    s_pulse_active = false;
}

/* 设置边框颜色 */
static void set_border_color(uint32_t color)
{
    lv_obj_set_style_border_color(s_overlay, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(s_overlay, LV_OPA_COVER, 0);
}

/* 设置标题 */
static void set_title(const char *text, uint32_t color)
{
    lv_label_set_text(s_title_label, text);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(color), 0);
}

/* 创建一个脉冲点 */
static lv_obj_t *create_pulse_dot(lv_obj_t *parent, lv_coord_t x_offset)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, 4, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, x_offset, -30);
    return dot;
}

void provision_screen_create(lv_obj_t *parent)
{
    if (s_overlay) return;

    /* 全屏覆盖容器 */
    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, 480, 480);
    lv_obj_center(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 3, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);

    /* 标题标签 */
    s_title_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 100);

    /* SSID 标签 */
    s_ssid_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);
    lv_obj_align(s_ssid_label, LV_ALIGN_CENTER, 0, -20);

    /* 详情标签 (IP 或错误) */
    s_detail_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_GREY), 0);
    lv_obj_align(s_detail_label, LV_ALIGN_CENTER, 0, 10);

    /* 状态胶囊标签 (BLE / Web) */
    s_status_capsule = lv_obj_create(s_overlay);
    lv_obj_set_size(s_status_capsule, 200, 32);
    lv_obj_align(s_status_capsule, LV_ALIGN_CENTER, 0, 60);
    lv_obj_clear_flag(s_status_capsule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_status_capsule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_status_capsule, 38, 0);
    lv_obj_set_style_radius(s_status_capsule, 16, 0);
    lv_obj_set_style_border_width(s_status_capsule, 1, 0);
    lv_obj_set_style_pad_all(s_status_capsule, 0, 0);

    s_status_capsule_lbl = lv_label_create(s_status_capsule);
    lv_obj_set_style_text_font(s_status_capsule_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(s_status_capsule_lbl);

    /* 提示标签 (底部) */
    s_hint_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(COLOR_GREY), 0);
    lv_obj_align(s_hint_label, LV_ALIGN_BOTTOM_MID, 0, -60);

    /* 进度条 */
    s_progress_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_progress_bar, 200, 6);
    lv_obj_align(s_progress_bar, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x30363d), 0);
    lv_obj_set_style_indic_color(s_progress_bar, lv_color_hex(COLOR_BLUE), 0);

    /* 脉冲点 (底部) */
    s_pulse_dots[0] = create_pulse_dot(s_overlay, -12);
    s_pulse_dots[1] = create_pulse_dot(s_overlay, 0);
    s_pulse_dots[2] = create_pulse_dot(s_overlay, 12);

    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_YELLOW), 0);
    }

    /* 默认状态: WAITING */
    provision_screen_set_state(PROV_SCREEN_WAITING, NULL, NULL);
}

void provision_screen_set_state(prov_screen_state_t state, const char *ssid, const char *detail)
{
    if (!s_overlay) return;

    pulse_stop();

    /* 隐藏进度条 (仅 CONNECTING 显示) */
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);

    switch (state) {
    case PROV_SCREEN_WAITING:
        set_border_color(COLOR_YELLOW);
        set_title("WiFi Setup", COLOR_YELLOW);

        lv_label_set_text(s_ssid_label, "PathFinder-EMOTE");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_hex(COLOR_BLUE), 0);

        lv_label_set_text(s_detail_label, "Connect to this hotspot");
        lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_GREY), 0);

        /* 胶囊: 显示 BLE + Web */
        lv_obj_set_style_bg_color(s_status_capsule, lv_color_hex(COLOR_BLUE), 0);
        lv_obj_set_style_bg_opa(s_status_capsule, 38, 0);
        lv_obj_set_style_border_color(s_status_capsule, lv_color_hex(COLOR_BLUE), 0);
        lv_obj_set_style_border_opa(s_status_capsule, LV_OPA_40, 0);
        lv_label_set_text(s_status_capsule_lbl, "BLE  +  Web  192.168.4.1");
        lv_obj_set_style_text_color(s_status_capsule_lbl, lv_color_hex(COLOR_BLUE), 0);

        lv_label_set_text(s_hint_label, "");

        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_YELLOW), 0);
        }
        pulse_start();
        break;

    case PROV_SCREEN_CONNECTING:
        set_border_color(COLOR_BLUE);
        set_title("Connecting...", COLOR_BLUE);

        lv_label_set_text(s_ssid_label, ssid ? ssid : "");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);

        lv_label_set_text(s_detail_label, detail ? detail : "");

        lv_obj_set_style_border_color(s_status_capsule, lv_color_hex(COLOR_GREY), 0);
        lv_obj_set_style_border_opa(s_status_capsule, LV_OPA_30, 0);
        lv_label_set_text(s_status_capsule_lbl, "");
        lv_obj_set_style_text_color(s_status_capsule_lbl, lv_color_hex(COLOR_GREY), 0);

        lv_label_set_text(s_hint_label, "");

        /* 显示进度条 */
        lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_range(s_progress_bar, 0, 100);
        lv_bar_set_value(s_progress_bar, 50, LV_ANIM_ON);

        /* 隐藏脉冲点 */
        for (int i = 0; i < 3; i++) {
            lv_obj_add_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
        break;

    case PROV_SCREEN_CONNECTED:
        set_border_color(COLOR_GREEN);
        set_title("Connected!", COLOR_GREEN);

        lv_label_set_text(s_ssid_label, ssid ? ssid : "");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);

        lv_label_set_text(s_detail_label, detail ? detail : "");
        lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_GREEN), 0);

        lv_label_set_text(s_hint_label, "Starting dashboard...");

        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_GREEN), 0);
        }
        pulse_start();
        break;

    case PROV_SCREEN_FAILED:
        set_border_color(COLOR_RED);
        set_title("Failed", COLOR_RED);

        lv_label_set_text(s_ssid_label, ssid ? ssid : "");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);

        lv_label_set_text(s_detail_label, detail ? detail : "Connection failed");
        lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_RED), 0);

        lv_label_set_text(s_hint_label, "Returning to setup...");

        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_RED), 0);
        }
        pulse_start();
        break;
    }
}

void provision_screen_destroy(void)
{
    pulse_stop();
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
        s_title_label = NULL;
        s_ssid_label = NULL;
        s_detail_label = NULL;
        s_hint_label = NULL;
        s_status_capsule = NULL;
        s_status_capsule_lbl = NULL;
        s_progress_bar = NULL;
        for (int i = 0; i < 3; i++) {
            s_pulse_dots[i] = NULL;
        }
    }
}

bool provision_screen_is_visible(void)
{
    return s_overlay != NULL;
}
```

- [ ] **Step 3: Commit**

```bash
git add PathFinder_EMOTE/main/provision_screen.h PathFinder_EMOTE/main/provision_screen.c
git commit -m "feat: add provision_screen LVGL overlay with 4 states (WAITING/CONNECTING/CONNECTED/FAILED)"
```

---

## Task 5: BLE C5 Write 特征值集成

**Files:**
- Modify: `PathFinder_EMOTE/main/ble_gatt_server.h`
- Modify: `PathFinder_EMOTE/main/ble_gatt_server.c`

- [ ] **Step 1: 在 ble_gatt_server.h 新增 C5 相关声明**

在文件末尾 `#endif` 之前插入：

```c
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
```

- [ ] **Step 2: 在 ble_gatt_server.c 中集成 C5 特征值**

在现有 C4 定义之后添加 C5 UUID 和全局变量：

```c
/* C5 WiFi: 0xFE05 */
static const ble_uuid16_t c5_wifi_uuid = BLE_UUID16_INIT(0xFE05);

static uint16_t s_c5_handle = 0;
static bool s_c5_subscribed = false;
static ble_wifi_write_cb_t s_wifi_write_cb = NULL;

/* JSON 分包缓冲 */
#define WIFI_JSON_BUF_SIZE 512
static char s_json_buf[WIFI_JSON_BUF_SIZE];
static int  s_json_buf_pos = 0;
```

- [ ] **Step 3: 修改 gatt_chr_access 回调处理 C5 Write**

替换现有的 `gatt_chr_access` 函数为：

```c
static int gatt_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t dummy[20] = {0};
        os_mbuf_append(ctxt->om, dummy, sizeof(dummy));
        return 0;
    }

    /* C5 Write — 接收配网 JSON */
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (attr_handle == s_c5_handle) {
            uint8_t data[200];
            uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
            if (data_len > sizeof(data) - 1) data_len = sizeof(data) - 1;
            ble_hs_mbuf_to_flat(ctxt->om, data, data_len, &data_len);
            data[data_len] = '\0';

            /* 分包处理: 首字节 0x00=完整帧, 0x01=分片后续 */
            if (data[0] == 0x00) {
                /* 新帧开始 */
                s_json_buf_pos = 0;
                size_t copy_len = data_len - 1;
                if (copy_len >= WIFI_JSON_BUF_SIZE) copy_len = WIFI_JSON_BUF_SIZE - 1;
                memcpy(s_json_buf, &data[1], copy_len);
                s_json_buf_pos = copy_len;
            } else if (data[0] == 0x01) {
                /* 分片后续 */
                size_t copy_len = data_len - 1;
                if (s_json_buf_pos + copy_len >= WIFI_JSON_BUF_SIZE) {
                    copy_len = WIFI_JSON_BUF_SIZE - 1 - s_json_buf_pos;
                }
                memcpy(&s_json_buf[s_json_buf_pos], &data[1], copy_len);
                s_json_buf_pos += copy_len;
            }

            s_json_buf[s_json_buf_pos] = '\0';

            /* 检查 JSON 是否完整 (闭合 }) */
            if (strchr(s_json_buf, '}') != NULL) {
                ESP_LOGI("ble_gatt", "收到完整 JSON: %s", s_json_buf);
                if (s_wifi_write_cb) {
                    s_wifi_write_cb(s_json_buf);
                }
                s_json_buf_pos = 0;
            }
            return 0;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}
```

- [ ] **Step 4: 在 gatt_svcs 数组中添加 C5 特征值**

在 C4 特征值定义之后、`{0}` 结束之前添加：

```c
            {
                .uuid = &c5_wifi_uuid.u,
                .access_cb = gatt_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_c5_handle,
            },
```

- [ ] **Step 5: 在 SUBSCRIBE 事件中添加 C5 订阅处理**

在 `gap_event` 的 `BLE_GAP_EVENT_SUBSCRIBE` case 中，在 C4 处理之后添加：

```c
            } else if (event->subscribe.attr_handle == s_c5_handle) {
                s_c5_subscribed = (event->subscribe.cur_notify == 1);
                ESP_LOGI(TAG, "C5(WiFi) %s", s_c5_subscribed ? "subscribed" : "unsubscribed");
            }
```

- [ ] **Step 6: 添加 notify 和回调注册函数**

在文件末尾添加：

```c
void ble_gatt_notify_wifi_status(const char *json_str)
{
    if (!s_initialized || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;
    if (!s_c5_subscribed)
        return;

    size_t len = strlen(json_str);
    if (len > 180) len = 180;

    struct os_mbuf *om = ble_hs_mbuf_from_flat((const uint8_t *)json_str, len);
    if (om) {
        ble_gattc_notify_custom(s_conn_handle, s_c5_handle, om);
    }
}

void ble_gatt_register_wifi_write_cb(ble_wifi_write_cb_t cb)
{
    s_wifi_write_cb = cb;
}
```

- [ ] **Step 7: 在 DISCONNECT 事件中清除 C5 订阅状态**

在 `gap_event` 的 `BLE_GAP_EVENT_DISCONNECT` case 中添加：

```c
        s_c5_subscribed = false;
```

- [ ] **Step 8: Commit**

```bash
git add PathFinder_EMOTE/main/ble_gatt_server.h PathFinder_EMOTE/main/ble_gatt_server.c
git commit -m "feat: add C5 (0xFE05) Write+Notify characteristic for BLE WiFi provisioning"
```

---

## Task 6: main.c 集成 wifi_config_manager + provision_screen

**Files:**
- Modify: `PathFinder_EMOTE/main/main.c`

- [ ] **Step 1: 添加头文件引用**

在 main.c 顶部 include 区添加（`#include "ble_gatt_server.h"` 之后）：

```c
#include "wifi_config_manager.h"
#include "web_portal.h"
#include "provision_screen.h"
#include "cJSON.h"
```

- [ ] **Step 2: 添加 Wi-Fi 状态回调函数**

在 `ui_create()` 之前添加：

```c
/* ── Wi-Fi 配网状态 → provision_screen 同步 ── */
static void on_wifi_prov_state(wifi_prov_state_t state, const char *ssid, const char *detail)
{
    if (!lvgl_lock(100)) return;

    switch (state) {
    case WIFI_PROV_STATE_PROVISIONING:
        if (!provision_screen_is_visible()) {
            provision_screen_create(lv_disp_get_scr_act(lv_disp_get_default()));
        }
        provision_screen_set_state(PROV_SCREEN_WAITING, NULL, NULL);
        break;

    case WIFI_PROV_STATE_CONNECTING:
        provision_screen_set_state(PROV_SCREEN_CONNECTING, ssid, detail);
        break;

    case WIFI_PROV_STATE_CONNECTED:
        provision_screen_set_state(PROV_SCREEN_CONNECTED, ssid, detail);
        /* 2 秒后销毁配网 UI */
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (lvgl_lock(100)) {
            provision_screen_destroy();
            lvgl_unlock();
        }
        break;

    case WIFI_PROV_STATE_FAILED:
        provision_screen_set_state(PROV_SCREEN_FAILED, ssid, detail);
        /* 3 秒后返回 WAITING (由 wifi_config_manager 重进配网模式驱动) */
        break;

    default:
        break;
    }

    /* 通过 BLE C5 Notify 通知 App */
    cJSON *json = cJSON_CreateObject();
    const char *state_str = "idle";
    switch (state) {
        case WIFI_PROV_STATE_PROVISIONING: state_str = "provisioning"; break;
        case WIFI_PROV_STATE_CONNECTING:   state_str = "connecting"; break;
        case WIFI_PROV_STATE_CONNECTED:    state_str = "connected"; break;
        case WIFI_PROV_STATE_FAILED:       state_str = "failed"; break;
        default: break;
    }
    cJSON_AddStringToObject(json, "status", state_str);
    if (ssid && strlen(ssid) > 0) cJSON_AddStringToObject(json, "ssid", ssid);
    if (detail && strlen(detail) > 0) cJSON_AddStringToObject(json, detail && state == WIFI_PROV_STATE_CONNECTED ? "ip" : "error", detail);
    char *json_str = cJSON_PrintUnformatted(json);
    ble_gatt_notify_wifi_status(json_str);
    cJSON_free(json_str);
    cJSON_Delete(json);

    lvgl_unlock();
}

/* ── BLE C5 Write 回调 — 解析 JSON 命令 ── */
static void on_ble_wifi_write(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cmd || !cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(cmd->valuestring, "set_wifi") == 0) {
        cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
        cJSON *pass_item = cJSON_GetObjectItem(root, "pass");
        if (ssid_item && cJSON_IsString(ssid_item)) {
            const char *ssid = ssid_item->valuestring;
            const char *pass = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";
            wifi_config_manager_set_credentials(ssid, pass);
        }
    } else if (strcmp(cmd->valuestring, "get_status") == 0) {
        /* 发送当前状态 */
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"status\":\"%s\"}",
                 wifi_config_manager_is_connected() ? "connected" : "provisioning");
        ble_gatt_notify_wifi_status(buf);
    } else if (strcmp(cmd->valuestring, "reset_wifi") == 0) {
        wifi_config_manager_reset();
    }

    cJSON_Delete(root);
}
```

- [ ] **Step 3: 在 app_main() 中集成**

在 `app_main()` 的 `ble_gatt_server_init()` 之后、`xTaskCreate(ble_notify_task, ...)` 之前插入：

```c
    /* ---- Wi-Fi 配置管理器 ---- */
    ESP_LOGI(TAG, "初始化 Wi-Fi 配置管理器");
    wifi_config_manager_register_cb(on_wifi_prov_state);
    ble_gatt_register_wifi_write_cb(on_ble_wifi_write);
    wifi_config_manager_init();
```

- [ ] **Step 4: 验证编译**

```bash
cd PathFinder_EMOTE
idf.py build
```

Expected: 编译成功。所有源文件和依赖都已就位。

- [ ] **Step 5: Commit**

```bash
git add PathFinder_EMOTE/main/main.c
git commit -m "feat: integrate wifi_config_manager and provision_screen into app_main"
```

---

## Task 7: Flutter 侧 — BLE UUID 重命名 + Write 封装

**Files:**
- Modify: `PathFinder_Dashboard/lib/core/ble/ble_uuids.dart`
- Create: `PathFinder_Dashboard/lib/core/ble/ble_wifi_writer.dart`
- Modify: `PathFinder_Dashboard/lib/core/ble/reactive_ble_service.dart`

- [ ] **Step 1: 重命名 ble_uuids.dart 中的 c5 UUID**

将 `c5RawImuUuid` 重命名为 `c5WifiUuid`：

```dart
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

// Service UUID
final pfServiceUuid = Uuid.parse('0000fe00-0000-1000-8000-00805f9b34fb');

// Characteristic UUIDs
final c1DeviceInfoUuid = Uuid.parse('0000fe01-0000-1000-8000-00805f9b34fb');
final c2EnvDataUuid = Uuid.parse('0000fe02-0000-1000-8000-00805f9b34fb');
final c3MotionDataUuid = Uuid.parse('0000fe03-0000-1000-8000-00805f9b34fb');
final c4EmoteStateUuid = Uuid.parse('0000fe04-0000-1000-8000-00805f9b34fb');
final c5WifiUuid = Uuid.parse('0000fe05-0000-1000-8000-00805f9b34fb');
```

- [ ] **Step 2: 创建 ble_wifi_writer.dart**

```dart
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'ble_uuids.dart';

/// BLE C5 (0xFE05) Write 封装 — 向 ESP32 发送 Wi-Fi 配网命令
class BleWifiWriter {
  final FlutterReactiveBle _ble;

  BleWifiWriter(this._ble);

  /// 发送 Wi-Fi 凭据到 ESP32
  Future<void> sendWifiConfig(String deviceId, String ssid, String password) async {
    final json = jsonEncode({
      'cmd': 'set_wifi',
      'ssid': ssid,
      'pass': password,
    });
    await _writeJson(deviceId, json);
  }

  /// 查询当前配网状态
  Future<void> queryStatus(String deviceId) async {
    await _writeJson(deviceId, jsonEncode({'cmd': 'get_status'}));
  }

  /// 清除 ESP32 的 Wi-Fi 凭据 (重新配网)
  Future<void> resetWifi(String deviceId) async {
    await _writeJson(deviceId, jsonEncode({'cmd': 'reset_wifi'}));
  }

  /// 内部: 发送 JSON, 支持 BLE MTU 分包
  Future<void> _writeJson(String deviceId, String json) async {
    final char = QualifiedCharacteristic(
      characteristicId: c5WifiUuid,
      serviceId: pfServiceUuid,
      deviceId: deviceId,
    );

    final bytes = utf8.encode(json);
    final mtu = 180; // BLE ATT MTU 安全上限

    if (bytes.length <= mtu - 1) {
      // 单帧: 前缀 0x00 + JSON
      final frame = Uint8List(bytes.length + 1);
      frame[0] = 0x00;
      frame.setRange(1, 1 + bytes.length, bytes);
      await _ble.writeCharacteristicWithoutResponse(char, value: frame.toList());
      debugPrint('[BLE WiFi] Sent single frame: $json');
    } else {
      // 分包: 首帧 0x00 + 后续帧 0x01
      int offset = 0;
      bool isFirst = true;

      while (offset < bytes.length) {
        final chunkSize = mtu - 1;
        final remaining = bytes.length - offset;
        final copyLen = remaining < chunkSize ? remaining : chunkSize;

        final frame = Uint8List(copyLen + 1);
        frame[0] = isFirst ? 0x00 : 0x01;
        frame.setRange(1, 1 + copyLen, bytes.sublist(offset, offset + copyLen));

        await _ble.writeCharacteristicWithoutResponse(char, value: frame.toList());
        debugPrint('[BLE WiFi] Sent ${isFirst ? "first" : "continuation"} frame: $copyLen bytes');

        offset += copyLen;
        isFirst = false;

        // 帧间小延迟
        await Future.delayed(const Duration(milliseconds: 20));
      }
    }
  }
}
```

- [ ] **Step 3: 在 reactive_ble_service.dart 中添加 writeWifiConfig 方法**

在文件末尾 `dispose()` 方法之前添加：

```dart
  // ── Wi-Fi 配网 ──

  /// 写入 Wi-Fi 配置到 ESP32 (通过 C5 特征值)
  Future<void> writeWifiConfig(String ssid, String password) async {
    if (_connectedDeviceId == null) {
      debugPrint('[BLE] Cannot write WiFi config: not connected');
      return;
    }
    final writer = BleWifiWriter(_ble);
    await writer.sendWifiConfig(_connectedDeviceId!, ssid, password);
  }

  /// 查询 ESP32 Wi-Fi 状态
  Future<void> queryWifiStatus() async {
    if (_connectedDeviceId == null) return;
    final writer = BleWifiWriter(_ble);
    await writer.queryStatus(_connectedDeviceId!);
  }

  /// 重置 ESP32 Wi-Fi 配置
  Future<void> resetWifiConfig() async {
    if (_connectedDeviceId == null) return;
    final writer = BleWifiWriter(_ble);
    await writer.resetWifi(_connectedDeviceId!);
  }
```

同时在文件顶部的 import 区添加：

```dart
import 'ble_wifi_writer.dart';
```

- [ ] **Step 4: 验证 Flutter 分析**

```bash
cd PathFinder_Dashboard
flutter analyze
```

Expected: 无错误 (可能有 info 级别提示, 可忽略)。

- [ ] **Step 5: Commit**

```bash
git add PathFinder_Dashboard/lib/core/ble/ble_uuids.dart PathFinder_Dashboard/lib/core/ble/ble_wifi_writer.dart PathFinder_Dashboard/lib/core/ble/reactive_ble_service.dart
git commit -m "feat: add BLE WiFi provisioning write support (C5 0xFE05)"
```

---

## Task 8: Flutter Wi-Fi 配网页面

**Files:**
- Create: `PathFinder_Dashboard/lib/features/wifi/wifi_setup_screen.dart`
- Modify: `PathFinder_Dashboard/lib/app/app.dart`

- [ ] **Step 1: 创建 wifi_setup_screen.dart**

```dart
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/ble/reactive_ble_service.dart';
import '../../shared/providers/ble_provider.dart';

/// Wi-Fi 配网页面 — 通过 BLE 向 ESP32 发送 Wi-Fi 凭据
class WifiSetupScreen extends ConsumerStatefulWidget {
  const WifiSetupScreen({super.key});

  @override
  ConsumerState<WifiSetupScreen> createState() => _WifiSetupScreenState();
}

enum _WifiStatus { idle, sending, connecting, connected, failed }

class _WifiSetupScreenState extends ConsumerState<WifiSetupScreen> {
  final _ssidController = TextEditingController();
  final _passController = TextEditingController();
  _WifiStatus _status = _WifiStatus.idle;
  String _statusDetail = '';

  @override
  void dispose() {
    _ssidController.dispose();
    _passController.dispose();
    super.dispose();
  }

  Future<void> _sendConfig() async {
    final ssid = _ssidController.text.trim();
    final pass = _passController.text;

    if (ssid.isEmpty) {
      setState(() => _statusDetail = '请输入 WiFi 名称');
      return;
    }

    setState(() {
      _status = _WifiStatus.sending;
      _statusDetail = '正在发送配置...';
    });

    final bleService = ref.read(bleServiceProvider);
    await bleService.writeWifiConfig(ssid, pass);

    setState(() {
      _status = _WifiStatus.connecting;
      _statusDetail = 'ESP32 正在连接 WiFi...';
    });
  }

  Future<void> _resetWifi() async {
    final bleService = ref.read(bleServiceProvider);
    await bleService.resetWifiConfig();
    setState(() {
      _status = _WifiStatus.idle;
      _statusDetail = '已清除 ESP32 WiFi 配置';
    });
  }

  @override
  Widget build(BuildContext context) {
    final bleState = ref.watch(bleServiceProvider).currentState;

    final isBleConnected = bleState == BleConnectionState.connected;

    return Scaffold(
      appBar: AppBar(
        title: const Text('WiFi 设置'),
      ),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // BLE 连接状态提示
            if (!isBleConnected)
              Card(
                color: Colors.orange.shade900.withValues(alpha: 0.3),
                child: const Padding(
                  padding: EdgeInsets.all(16),
                  child: Row(
                    children: [
                      Icon(Icons.warning, color: Colors.orange),
                      SizedBox(width: 12),
                      Expanded(
                        child: Text(
                          '请先连接 BLE 设备',
                          style: TextStyle(color: Colors.orange),
                        ),
                      ),
                    ],
                  ),
                ),
              ),

            const SizedBox(height: 16),

            // SSID 输入
            TextField(
              controller: _ssidController,
              decoration: const InputDecoration(
                labelText: 'WiFi 名称 (SSID)',
                prefixIcon: Icon(Icons.wifi),
                border: OutlineInputBorder(),
              ),
              enabled: isBleConnected && _status != _WifiStatus.sending,
            ),

            const SizedBox(height: 16),

            // 密码输入
            TextField(
              controller: _passController,
              obscureText: true,
              decoration: const InputDecoration(
                labelText: 'WiFi 密码',
                prefixIcon: Icon(Icons.lock),
                border: OutlineInputBorder(),
              ),
              enabled: isBleConnected && _status != _WifiStatus.sending,
            ),

            const SizedBox(height: 24),

            // 发送按钮
            FilledButton.icon(
              onPressed: (isBleConnected && _status != _WifiStatus.sending)
                  ? _sendConfig
                  : null,
              icon: const Icon(Icons.send),
              label: const Text('发送配置到 ESP32'),
            ),

            const SizedBox(height: 12),

            // 重置按钮
            OutlinedButton.icon(
              onPressed: isBleConnected ? _resetWifi : null,
              icon: const Icon(Icons.refresh),
              label: const Text('清除 ESP32 WiFi 配置'),
            ),

            const SizedBox(height: 24),

            // 状态显示
            if (_statusDetail.isNotEmpty)
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Row(
                    children: [
                      Icon(
                        _status == _WifiStatus.connecting
                            ? Icons.hourglass_top
                            : _status == _WifiStatus.connected
                                ? Icons.check_circle
                                : _status == _WifiStatus.failed
                                    ? Icons.error
                                    : Icons.info,
                        color: _status == _WifiStatus.connecting
                            ? Colors.blue
                            : _status == _WifiStatus.connected
                                ? Colors.green
                                : _status == _WifiStatus.failed
                                    ? Colors.red
                                    : Colors.grey,
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: Text(
                          _statusDetail,
                          style: TextStyle(
                            color: _status == _WifiStatus.connecting
                                ? Colors.blue
                                : _status == _WifiStatus.connected
                                    ? Colors.green
                                    : _status == _WifiStatus.failed
                                        ? Colors.red
                                        : Colors.grey,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),

            const Spacer(),

            // 说明文字
            Text(
              '通过 BLE 向 ESP32 发送 WiFi 配置。'
              '\n设备连接 WiFi 后将自动关闭配网模式。',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Colors.grey,
                  ),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}
```

- [ ] **Step 2: 在 ble_provider.dart 中暴露 currentState**

检查 `PathFinder_Dashboard/lib/shared/providers/ble_provider.dart` 是否有暴露 `currentState` 的方法。如果没有，需要确保 `bleServiceProvider` 返回的服务实例可以访问 `currentState`。

```bash
cd PathFinder_Dashboard
grep -n "currentState\|bleServiceProvider" lib/shared/providers/ble_provider.dart
```

如果 `ble_provider.dart` 中没有暴露当前状态，添加一个 getter。但基于现有代码，`ReactiveBleService` 已经有 `_currentState` 字段。需要在接口中暴露。

在 `ble_service_interface.dart` 中添加：

```dart
BleConnectionState get currentState;
```

在 `reactive_ble_service.dart` 中添加 getter：

```dart
@override
BleConnectionState get currentState => _currentState;
```

- [ ] **Step 3: 在 app.dart 中添加 WiFi 设置入口**

在 `app.dart` 的 AppBar actions 中，`BleStatusChip` 之后添加 WiFi 设置图标按钮：

```dart
              const BleStatusChip(),
              IconButton(
                icon: const Icon(Icons.wifi_settings, size: 22),
                onPressed: () => _navigateToWifiSetup(context),
                tooltip: 'WiFi 设置',
              ),
```

并添加导航方法（在 `_navigateToHistory` 之后）：

```dart
  void _navigateToWifiSetup(BuildContext context) {
    Navigator.of(context).push(
      MaterialPageRoute(builder: (_) => const WifiSetupScreen()),
    );
  }
```

同时在文件顶部添加 import：

```dart
import '../features/wifi/wifi_setup_screen.dart';
```

- [ ] **Step 4: 验证 Flutter 分析**

```bash
cd PathFinder_Dashboard
flutter analyze
```

Expected: 无错误。

- [ ] **Step 5: Commit**

```bash
git add PathFinder_Dashboard/lib/features/wifi/wifi_setup_screen.dart PathFinder_Dashboard/lib/app/app.dart PathFinder_Dashboard/lib/core/ble/ble_service_interface.dart PathFinder_Dashboard/lib/core/ble/reactive_ble_service.dart
git commit -m "feat: add Flutter WiFi setup screen with BLE provisioning UI"
```

---

## Task 9: 端到端验证

- [ ] **Step 1: 固件完整编译**

```bash
cd PathFinder_EMOTE
idf.py build
```

Expected: 编译成功，无错误。

- [ ] **Step 2: 固件烧录**

```bash
idf.py flash
```

- [ ] **Step 3: 验证首次开机进入配网模式**

清除 NVS 后重启：

```bash
idf.py erase-flash flash
```

Expected: 串口日志显示 `无凭据, 启动配网模式`，LCD 显示 WiFi Setup 状态页，手机可搜到 `PathFinder-EMOTE` 热点。

- [ ] **Step 4: 验证 Web Portal 配网**

1. 手机连接 `PathFinder-EMOTE` 热点
2. 等待 Captive Portal 自动弹出
3. 从列表选择一个 Wi-Fi
4. 输入密码，点击 Connect
5. 等待连接成功

Expected: LCD 显示 "Connected!" 2 秒后切换到正常 EAF UI。手机热点列表中不再出现 `PathFinder-EMOTE`。

- [ ] **Step 5: 验证 BLE 配网**

1. 清除 NVS 重进配网模式
2. Flutter App 连接 BLE 设备
3. 进入 WiFi 设置页面
4. 输入 SSID + 密码，点击发送
5. 观察 LCD 显示 Connecting → Connected

Expected: 配网成功，EAF 正常 UI 恢复。

- [ ] **Step 6: 验证已有凭据自动连接**

重启设备（不擦除 NVS）。

Expected: 串口日志显示 `有凭据, 启动 STA 连接`，直接进入正常模式，无配网页。

- [ ] **Step 7: 验证内存回收**

在配网成功后通过串口观察：

```
ESP_LOGI(TAG, "Free heap after provisioning: %lu", esp_get_free_heap_size());
```

Expected: 配网成功后可用堆内存比配网期间增加约 30KB。

- [ ] **Step 8: 最终 Commit**

```bash
git add -A
git commit -m "test: end-to-end verification of BLE + Web dual-mode WiFi provisioning"
```
