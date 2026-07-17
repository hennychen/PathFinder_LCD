/**
 * web_viewer.c — WiFi AP + HTTP server for camera preview.
 *
 * Uses a shared snapshot buffer protected by a mutex to avoid
 * concurrent esp_camera_fb_get() conflicts with the vision task.
 */

#include "web_viewer.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_jpeg_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web_viewer";

/* ---- Shared snapshot (updated by timer, read by HTTP) ---- */
static SemaphoreHandle_t s_snap_mutex = NULL;
static uint8_t *s_snap_buf = NULL;        /* RGB565 snapshot */
static int s_snap_w = 0;
static int s_snap_h = 0;
static bool s_snap_valid = false;

/* ---- JPEG encoder ---- */
static jpeg_enc_handle_t s_jpeg_enc = NULL;
static uint8_t *s_jpeg_out = NULL;
#define JPEG_OUT_SIZE  (FACE_IMG_WIDTH * FACE_IMG_HEIGHT * 2 + 4096)

/* ---- HTTP server ---- */
static httpd_handle_t s_server = NULL;

/* ---- HTML page ---- */
static const char PAGE_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Tracker Camera</title>"
"<style>"
"body{background:#111;color:#eee;font-family:monospace;text-align:center;margin:0;padding:20px}"
"h1{font-size:1.4em;margin:10px}"
"img{max-width:90vw;border:2px solid #444;border-radius:8px}"
".info{margin-top:10px;font-size:0.8em;color:#888}"
"</style>"
"<script>"
"var last=Date.now();"
"function refresh(){"
"  var img=document.getElementById('cam');"
"  img.src='/snap?'+Date.now();"
"  img.onload=function(){"
"    var now=Date.now();"
"    document.getElementById('fps').textContent=(1000/(now-last)).toFixed(1);"
"    last=now;setTimeout(refresh,200);"
"  };"
"  img.onerror=function(){setTimeout(refresh,1000);};"
"}"
"</script></head>"
"<body onload='refresh()'>"
"<h1>PathFinder Tracker Camera</h1>"
"<img id='cam' src='/snap'>"
"<div class='info'>FPS: <span id='fps'>-</span> | 320x240 RGB565</div>"
"</body></html>";

/* ---- Snapshot capture task ---- */
static void snap_task(void *arg)
{
    (void)arg;
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb && fb->format == PIXFORMAT_RGB565) {
            if (xSemaphoreTake(s_snap_mutex, pdMS_TO_TICKS(50))) {
                int copy_size = fb->width * fb->height * 2;
                int max_size = FACE_IMG_WIDTH * FACE_IMG_HEIGHT * 2;
                if (copy_size > max_size) copy_size = max_size;
                memcpy(s_snap_buf, fb->buf, copy_size);
                s_snap_w = fb->width;
                s_snap_h = fb->height;
                s_snap_valid = true;
                xSemaphoreGive(s_snap_mutex);
            }
            esp_camera_fb_return(fb);
        } else if (fb) {
            esp_camera_fb_return(fb);
        }
        vTaskDelay(pdMS_TO_TICKS(200));  /* 5 fps snapshot */
    }
}

/* ---- HTTP handlers ---- */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_snap(httpd_req_t *req)
{
    if (!s_snap_valid || !s_snap_mutex) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /* Lock snapshot and encode to JPEG */
    if (!xSemaphoreTake(s_snap_mutex, pdMS_TO_TICKS(100))) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int out_size = 0;
    jpeg_error_t jret = jpeg_enc_process(s_jpeg_enc,
                                         s_snap_buf,
                                         s_snap_w * s_snap_h * 2,
                                         s_jpeg_out, JPEG_OUT_SIZE,
                                         &out_size);
    xSemaphoreGive(s_snap_mutex);

    if (jret != JPEG_ERR_OK || out_size <= 0) {
        ESP_LOGW(TAG, "JPEG encode failed: %d", (int)jret);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, (char *)s_jpeg_out, out_size);
    return ESP_OK;
}

/* ---- WiFi AP ---- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg; (void)event_base;
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = event_data;
        ESP_LOGI(TAG, "Device connected: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    }
}

static esp_err_t start_wifi_ap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "TrackerDev",
            .ssid_len = strlen("TrackerDev"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP: SSID=TrackerDev IP=192.168.4.1");
    return ESP_OK;
}

/* ---- Public API ---- */
esp_err_t web_viewer_start(void)
{
    esp_err_t ret;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    ret = start_wifi_ap();
    if (ret != ESP_OK) return ret;

    /* JPEG encoder: try RGB565 BE (ESP32-S3 DVP DMA stores big-endian) */
    jpeg_enc_config_t enc_cfg = {
        .width       = FACE_IMG_WIDTH,
        .height      = FACE_IMG_HEIGHT,
        .src_type    = JPEG_PIXEL_FORMAT_RGB565_BE,
        .subsampling = JPEG_SUBSAMPLE_420,
        .quality     = 60,
        .rotate      = JPEG_ROTATE_0D,
        .task_enable = false,
    };
    jpeg_error_t jret = jpeg_enc_open(&enc_cfg, &s_jpeg_enc);
    if (jret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG enc open failed: %d", (int)jret);
        return ESP_FAIL;
    }

    /* Allocate buffers */
    s_snap_buf = (uint8_t *)heap_caps_malloc(
        FACE_IMG_WIDTH * FACE_IMG_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    s_jpeg_out = (uint8_t *)malloc(JPEG_OUT_SIZE);
    s_snap_mutex = xSemaphoreCreateMutex();

    if (!s_snap_buf || !s_jpeg_out || !s_snap_mutex) {
        ESP_LOGE(TAG, "Buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* Start snapshot capture task on Core 0 (low priority) */
    xTaskCreatePinnedToCore(snap_task, "snap", 4096, NULL, 1, NULL, 0);

    /* Start HTTP server */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed");
        return ESP_FAIL;
    }

    httpd_uri_t uri_root = { .uri="/", .method=HTTP_GET, .handler=handler_root };
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_uri_t uri_snap = { .uri="/snap", .method=HTTP_GET, .handler=handler_snap };
    httpd_register_uri_handler(s_server, &uri_snap);

    ESP_LOGI(TAG, "=== Web Viewer Ready ===");
    ESP_LOGI(TAG, "WiFi: TrackerDev / 12345678");
    ESP_LOGI(TAG, "URL:  http://192.168.4.1/");
    return ESP_OK;
}
