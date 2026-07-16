/**
 * @file vision_task.h
 * @brief Core-1 vision task and dual-core message queue.
 *
 * The vision task runs exclusively on Core 1 (capture → detect → publish),
 * while Core 0 handles audio localisation and the main control loop.
 * Results are exchanged via a lock-free overwrite queue.
 */

#ifndef VISION_TASK_H
#define VISION_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Snapshot of the latest vision result, consumed by Core 0.
 */
typedef struct {
    bool     face_found;   /**< true if at least one face was detected   */
    int16_t  face_cx;      /**< face centre X (pixels)                   */
    int16_t  face_cy;      /**< face centre Y (pixels)                   */
    uint16_t face_w;       /**< face box width  (pixels)                 */
    uint16_t face_h;       /**< face box height (pixels)                 */
    uint32_t inference_ms; /**< last ESP-DL inference time               */
} vision_msg_t;

/**
 * @brief Create the vision queue and launch the Core-1 vision task.
 *
 * Must be called after drv_ov2640_init().
 *
 * @return ESP_OK on success.
 */
esp_err_t vision_task_init(void);

/**
 * @brief Non-blocking read of the most recent vision message.
 *
 * Safe to call from any core / ISR-safe context with zero wait.
 *
 * @param msg  Output buffer.
 * @return true if a message was available, false otherwise.
 */
bool vision_get_latest(vision_msg_t *msg);

#endif /* VISION_TASK_H */
