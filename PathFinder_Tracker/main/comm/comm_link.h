/**
 * @file comm_link.h
 * @brief Unified communication dispatcher — ESP-NOW > Mesh > UART fallback.
 *
 * Provides a single API for sending tracking data to Board A.
 * Internally selects the best available transport in priority order:
 *   1. ESP-NOW (fastest, <1ms, unreliable)
 *   2. ESP-WIFI-MESH P2P (reliable, 8-12ms)
 *   3. UART wired (backup, always available)
 */
#ifndef COMM_LINK_H
#define COMM_LINK_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the communication link subsystem.
 *        Must be called after mesh_node_init() and mesh_espnow_init().
 */
esp_err_t comm_link_init(void);

/**
 * @brief Send sound-source angle to Board A via best available channel.
 * @param angle  Angle in degrees [0, 360)
 * @param valid  1 = valid angle, 0 = invalid/no-signal
 * @return ESP_OK if at least one channel succeeded.
 */
esp_err_t comm_link_send_angle(float angle, uint8_t valid);

/**
 * @brief Send tracker state to Board A.
 * @param state  Tracker state enum value
 */
esp_err_t comm_link_send_state(uint8_t state);

/**
 * @brief Send face info to Board A.
 * @param face_found  Whether a face was detected
 * @param cx  Face center X (image coordinates)
 * @param cy  Face center Y
 * @param w   Face box width
 * @param h   Face box height
 */
esp_err_t comm_link_send_face_info(uint8_t face_found, int16_t cx, int16_t cy,
                                     uint16_t w, uint16_t h);

/**
 * @brief Send heartbeat to Board A (called periodically).
 */
esp_err_t comm_link_send_heartbeat(void);

/**
 * @brief Check if wireless communication (ESP-NOW or Mesh) is active.
 */
bool comm_link_wireless_ready(void);

#endif /* COMM_LINK_H */
