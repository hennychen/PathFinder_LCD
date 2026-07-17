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

    /* 如果 AP 模式在运行, 先停 Web Portal */
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

    /* 创建默认事件循环 (ESP-IDF v6.0 无 get_handle, 直接创建) */
    esp_err_t loop_ret = esp_event_loop_create_default();
    if (loop_ret != ESP_OK && loop_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(loop_ret);
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

esp_err_t wifi_config_manager_skip_provisioning(void)
{
    ESP_LOGI(TAG, "跳过配网, 停止 AP 模式 + Web Portal");

    /* 停止 Web Portal HTTP 服务器 */
    if (s_state == WIFI_PROV_STATE_PROVISIONING) {
        web_portal_stop();
    }

    /* 切换到 NULL 模式以关闭 AP, 降低功耗 */
    if (s_wifi_started) {
        esp_wifi_set_mode(WIFI_MODE_NULL);
    }

    s_state = WIFI_PROV_STATE_IDLE;
    return ESP_OK;
}

const char *wifi_config_manager_get_router_ssid(void)
{
    return s_ssid;
}

const char *wifi_config_manager_get_router_pass(void)
{
    return s_pass;
}
