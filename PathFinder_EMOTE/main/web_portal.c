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
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_portal";

static httpd_handle_t s_server = NULL;

/* ── DNS 劫持服务器 (Captive Portal 必备) ── */
/* 把所有域名解析到 192.168.4.1, 让手机探测请求到达我们的 HTTP Server */
static int s_dns_sock = -1;
static TaskHandle_t s_dns_task_handle = NULL;

static void dns_hijack_task(void *pv)
{
    struct sockaddr_in srv, cli;
    socklen_t cli_len;
    uint8_t rx[512], tx[512];

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "DNS socket 创建失败");
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(53);
    srv.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s_dns_sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        ESP_LOGE(TAG, "DNS bind 失败");
        close(s_dns_sock);
        s_dns_sock = -1;
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS 劫持已启动 (所有域名 → 192.168.4.1)");

    while (s_dns_sock >= 0) {
        cli_len = sizeof(cli);
        int len = recvfrom(s_dns_sock, rx, sizeof(rx), 0,
                           (struct sockaddr *)&cli, &cli_len);
        if (len < 12) continue;

        /* 构造 DNS 响应: QR=1, RA=1, 1 个 Answer */
        memcpy(tx, rx, len);
        tx[2] |= 0x80;  /* QR = response */
        tx[3] |= 0x80;  /* RA = recursion available */
        tx[6] = 0; tx[7] = 1;  /* ANCOUNT = 1 */

        int pos = len;  /* Answer 紧跟在 Question 后面 */

        /* 压缩指针指向 offset 12 (Question 的 QNAME) */
        tx[pos++] = 0xC0;
        tx[pos++] = 0x0C;
        /* TYPE A */
        tx[pos++] = 0x00; tx[pos++] = 0x01;
        /* CLASS IN */
        tx[pos++] = 0x00; tx[pos++] = 0x01;
        /* TTL = 60s */
        tx[pos++] = 0x00; tx[pos++] = 0x00;
        tx[pos++] = 0x00; tx[pos++] = 0x3C;
        /* RDLENGTH = 4 */
        tx[pos++] = 0x00; tx[pos++] = 0x04;
        /* RDATA = 192.168.4.1 */
        tx[pos++] = 192;
        tx[pos++] = 168;
        tx[pos++] = 4;
        tx[pos++] = 1;

        sendto(s_dns_sock, tx, pos, 0,
               (struct sockaddr *)&cli, cli_len);
    }

    ESP_LOGI(TAG, "DNS 劫持已停止");
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ── 配网页面 HTML (Flash RODATA, 不占 RAM) ── */
static const char PORTAL_HTML[] =
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
"<h2>PathFinder WiFi</h2>"
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
"'Connected to '+d.ssid+' ('+d.ip+')';}"
"else if(d.state==='failed'){document.getElementById('status').innerHTML="
"'Failed: '+d.detail;document.getElementById('btn').disabled=false;}"
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

/* ── Captive Portal 探测处理: 直接返回配网页面 ── */
/* 很多手机的 Captive Portal mini-browser 不跟随 302 重定向, */
/* 必须直接返回 HTML 内容才能弹出配网页面 */
static esp_err_t handler_captive(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
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

    /* Captive Portal 探测路径 (直接返回配网页面, 不重定向) */
    httpd_uri_t uri_gen204 = { .uri = "/generate_204", .method = HTTP_GET, .handler = handler_captive };
    httpd_uri_t uri_hotspot = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handler_captive };
    httpd_uri_t uri_ncsi = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = handler_captive };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_connect);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_gen204);
    httpd_register_uri_handler(s_server, &uri_hotspot);
    httpd_register_uri_handler(s_server, &uri_ncsi);

    /* 启动 DNS 劫持 (让手机 Captive Portal 探测到达 HTTP Server) */
    if (s_dns_task_handle == NULL) {
        xTaskCreate(dns_hijack_task, "dns_hijack", 3072, NULL, 3, &s_dns_task_handle);
    }

    ESP_LOGI(TAG, "Web Portal 已启动 (HTTP 端口 80 + DNS 端口 53)");
    return ESP_OK;
}

esp_err_t web_portal_stop(void)
{
    if (!s_server) return ESP_OK;

    /* 停止 HTTP Server */
    httpd_stop(s_server);
    s_server = NULL;

    /* 停止 DNS 劫持 */
    if (s_dns_sock >= 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    /* dns_hijack_task 检测到 s_dns_sock < 0 后会自动退出 */

    ESP_LOGI(TAG, "Web Portal + DNS 已停止, 内存已释放");
    return ESP_OK;
}

bool web_portal_is_running(void)
{
    return s_server != NULL;
}
