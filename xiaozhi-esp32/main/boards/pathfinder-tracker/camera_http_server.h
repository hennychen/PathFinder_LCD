#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the camera HTTP preview server on port 8080.
 *
 * Serves JPEG snapshots from the OV2640 camera at:
 *   http://<board-ip>:8080/        — HTML preview page
 *   http://<board-ip>:8080/cam      — Single JPEG frame
 *   http://<board-ip>:8080/stream   — MJPEG stream
 *
 * Must be called AFTER camera init AND AFTER WiFi is connected.
 * Uses esp_camera_fb_get() directly — thread-safe with Esp32Camera::Capture().
 *
 * @return ESP_OK on success.
 */
esp_err_t camera_http_server_start(void);

/**
 * @brief Acquire the shared camera framebuffer mutex.
 *
 * Must be called before esp_camera_fb_get() when multiple tasks
 * (HTTP server, face tracker) access the camera concurrently.
 *
 * @return true if mutex acquired within 500ms, false on timeout.
 */
bool camera_fb_lock(void);

/**
 * @brief Release the shared camera framebuffer mutex.
 */
void camera_fb_unlock(void);

#ifdef __cplusplus
}
#endif
