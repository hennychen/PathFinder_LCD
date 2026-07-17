#ifndef WEB_VIEWER_H
#define WEB_VIEWER_H

#include "esp_err.h"
#include "drv_ov2640.h"

/**
 * @brief Start the web viewer: WiFi AP + HTTP server.
 *
 * Creates a SoftAP "TrackerDev" (password: 12345678) and serves
 * the camera image on http://192.168.4.1/
 *
 * Call AFTER camera init.
 */
esp_err_t web_viewer_start(void);

#endif /* WEB_VIEWER_H */
