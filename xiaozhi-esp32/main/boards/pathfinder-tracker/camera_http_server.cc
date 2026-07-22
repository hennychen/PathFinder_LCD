/*
 * camera_http_server.cc — Live camera preview HTTP server.
 *
 * Runs on port 8080 alongside xiaozhi-esp32's main services.
 * Uses esp32-camera's built-in frame2jpg() for hardware-accelerated
 * JPEG conversion from RGB565, no external JPEG encoder needed.
 *
 * Endpoints:
 *   GET /          — HTML preview page (auto-refresh)
 *   GET /cam       — Single JPEG snapshot
 *   GET /stream    — MJPEG multipart stream
 */

#include "camera_http_server.h"

#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "cam_http";

/* ── Capture mutex (shared with Esp32Camera::Capture & face_tracker) ── */
static SemaphoreHandle_t s_cam_mutex = NULL;
static StaticSemaphore_t s_cam_mutex_buffer;
static bool s_started = false;

/* ── Public lock/unlock for external camera users (face_tracker) ── */
bool camera_fb_lock(void)
{
    if (!s_cam_mutex) return false;
    return xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(500));
}

void camera_fb_unlock(void)
{
    if (s_cam_mutex) xSemaphoreGive(s_cam_mutex);
}

/* ── HTML preview page ── */
static const char PAGE_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>PathFinder Camera</title>"
"<style>"
"body{background:#111;color:#eee;font-family:monospace;text-align:center;margin:0;padding:20px}"
"h1{font-size:1.3em;margin:8px}"
"img{max-width:95vw;border:2px solid #444;border-radius:8px}"
".info{margin-top:8px;font-size:0.8em;color:#888}"
"a{color:#4af;text-decoration:none}"
"</style>"
"<script>"
"var last=Date.now(),count=0;"
"function refresh(){"
"  var img=document.getElementById('cam');"
"  img.src='/cam?'+Date.now();"
"  img.onload=function(){"
"    var now=Date.now();count++;"
"    if(now-last>=1000){"
"      document.getElementById('fps').textContent=count;count=0;last=now;"
"    }"
"    setTimeout(refresh,200);"
"  };"
"  img.onerror=function(){setTimeout(refresh,1000);};"
"}"
"</script></head>"
"<body onload='refresh()'>"
"<h1>PathFinder Tracker Camera</h1>"
"<img id='cam' src='/cam'>"
"<div class='info'>FPS: <span id='fps'>-</span> | VGA 640x480 | "
"<a href='/stream'>MJPEG Stream</a></div>"
"</body></html>";

/* ── HTTP: root HTML page ── */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ── Capture + convert to JPEG (thread-safe) ──
 * Returns malloc'd JPEG buffer (caller must free), or NULL on failure.
 * Sets *out_len to JPEG size. */
static uint8_t *capture_jpeg(size_t *out_len)
{
    if (!s_cam_mutex || !xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(500))) {
        return NULL;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        xSemaphoreGive(s_cam_mutex);
        ESP_LOGW(TAG, "fb_get failed");
        return NULL;
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    /* frame2jpg handles both RGB565 and JPEG pixel formats */
    bool ok = frame2jpg(fb, 60, &jpg_buf, &jpg_len);
    esp_camera_fb_return(fb);
    xSemaphoreGive(s_cam_mutex);

    if (!ok || !jpg_buf || jpg_len == 0) {
        ESP_LOGW(TAG, "frame2jpg failed");
        return NULL;
    }
    *out_len = jpg_len;
    return jpg_buf;
}

/* ── HTTP: single JPEG snapshot ── */
static esp_err_t handler_cam(httpd_req_t *req)
{
    size_t jpg_size = 0;
    uint8_t *jpg = capture_jpeg(&jpg_size);
    if (!jpg) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_send(req, (char *)jpg, jpg_size);
    free(jpg);
    return ret;
}

/* ── HTTP: MJPEG stream ── */
static const char *BOUNDARY = "frame";
static esp_err_t handler_stream(httpd_req_t *req)
{
    httpd_resp_set_type(req,
        "multipart/x-mixed-replace;boundary=frame");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    while (true) {
        size_t jpg_size = 0;
        uint8_t *jpg = capture_jpeg(&jpg_size);
        if (!jpg) {
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        char hdr[128];
        int hdr_len = snprintf(hdr, sizeof(hdr),
            "\r\n--%s\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n\r\n",
            BOUNDARY, (unsigned)jpg_size);
        if (httpd_resp_send_chunk(req, hdr, hdr_len) != ESP_OK) {
            free(jpg);
            break;
        }
        if (httpd_resp_send_chunk(req, (char *)jpg, jpg_size) != ESP_OK) {
            free(jpg);
            break;
        }
        free(jpg);
        vTaskDelay(pdMS_TO_TICKS(200));  /* ~5 FPS */
    }
    return ESP_OK;
}

/* ── Public API ── */
esp_err_t camera_http_server_start(void)
{
    if (s_started) {
        ESP_LOGW(TAG, "Already started");
        return ESP_ERR_INVALID_STATE;
    }

    s_cam_mutex = xSemaphoreCreateMutexStatic(&s_cam_mutex_buffer);
    if (!s_cam_mutex) {
        ESP_LOGE(TAG, "Mutex alloc failed");
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_uri_handlers = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed on port 8080");
        return ESP_FAIL;
    }

    httpd_uri_t uri_root   = { .uri="/",        .method=HTTP_GET, .handler=handler_root   };
    httpd_uri_t uri_cam    = { .uri="/cam",     .method=HTTP_GET, .handler=handler_cam    };
    httpd_uri_t uri_stream = { .uri="/stream",  .method=HTTP_GET, .handler=handler_stream };
    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_cam);
    httpd_register_uri_handler(server, &uri_stream);

    s_started = true;
    ESP_LOGI(TAG, "=== Camera Preview Ready on port 8080 ===");
    ESP_LOGI(TAG, "  http://<board-ip>:8080/      (HTML page)");
    ESP_LOGI(TAG, "  http://<board-ip>:8080/cam   (JPEG snapshot)");
    ESP_LOGI(TAG, "  http://<board-ip>:8080/stream(MJPEG stream)");
    return ESP_OK;
}
