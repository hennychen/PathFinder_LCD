/**
 * @file mesh_espnow.h
 * @brief ESP-NOW communication layer for low-latency inter-board data.
 *
 * ESP-NOW operates on top of the Wi-Fi driver. When Mesh is started,
 * ESP-NOW automatically adapts to the Mesh channel.
 *
 * Usage:
 *   1. mesh_espnow_init()  — after Wi-Fi/Mesh is initialized
 *   2. mesh_espnow_register_rx_cb()  — set receive callback
 *   3. mesh_espnow_send()  — send data to peer
 */
#ifndef MESH_ESPNOW_H
#define MESH_ESPNOW_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* ESP-NOW broadcast MAC (all zeros = use peer MAC directly) */
extern const uint8_t ESPNOW_BROADCAST_MAC[6];

/* Receive callback type */
typedef void (*mesh_espnow_rx_cb_t)(const uint8_t *src_mac,
                                     const uint8_t *data, int data_len);

/**
 * @brief Initialize ESP-NOW.
 *        Must be called after Wi-Fi/Mesh is started.
 * @return ESP_OK or error code.
 */
esp_err_t mesh_espnow_init(void);

/**
 * @brief Register a receive callback.
 */
void mesh_espnow_register_rx_cb(mesh_espnow_rx_cb_t cb);

/**
 * @brief Send data to a peer via ESP-NOW.
 * @param target_mac  Peer MAC address (use ESPNOW_BROADCAST_MAC for broadcast)
 * @param msg_type    Message type from mesh_msg_type_t
 * @param data        Payload data
 * @param data_len    Payload length (max ESPNOW_MAX_PAYLOAD)
 * @return ESP_OK on success, ESP_FAIL otherwise.
 */
esp_err_t mesh_espnow_send(const uint8_t *target_mac, uint8_t msg_type,
                            const uint8_t *data, uint8_t data_len);

/**
 * @brief Add a peer to the ESP-NOW peer list.
 *        Called automatically on first send, but can be pre-registered.
 */
esp_err_t mesh_espnow_add_peer(const uint8_t *mac);

/**
 * @brief Check if ESP-NOW is initialized and ready.
 */
bool mesh_espnow_is_ready(void);

/**
 * @brief Get the local MAC address.
 */
esp_err_t mesh_espnow_get_local_mac(uint8_t mac[6]);

#endif /* MESH_ESPNOW_H */
