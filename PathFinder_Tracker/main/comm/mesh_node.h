/**
 * @file mesh_node.h
 * @brief ESP-WIFI-MESH node manager for inter-board networking.
 *
 * Board A (PathFinder_EMOTE) = ROOT  — connects to router, bridges external IP
 * Board B (PathFinder_Tracker) = CHILD — joins mesh via Board A
 *
 * Usage (CHILD / Board B):
 *   mesh_node_init(MESH_ROLE_CHILD);
 *   mesh_node_start();
 *   mesh_node_send(root_addr, data, len);
 *
 * Usage (ROOT / Board A):
 *   mesh_node_init(MESH_ROLE_ROOT);
 *   mesh_node_start();
 */
#ifndef MESH_NODE_H
#define MESH_NODE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* ── Mesh node roles ── */
typedef enum {
    MESH_ROLE_CHILD = 0,
    MESH_ROLE_ROOT  = 1,
} mesh_role_t;

/* ── Mesh link state ── */
typedef enum {
    MESH_LINK_IDLE = 0,
    MESH_LINK_CONNECTING,
    MESH_LINK_CONNECTED,
    MESH_LINK_DISCONNECTED,
} mesh_link_state_t;

/* ── Receive callback (called from mesh RX task) ── */
typedef void (*mesh_node_rx_cb_t)(const uint8_t *from_mac,
                                   const uint8_t *data, int data_len);

/**
 * @brief Initialize the mesh node with the given role.
 *        Must be called after NVS init, esp_netif_init, and Wi-Fi init.
 * @param role ROOT or CHILD
 * @return ESP_OK or error.
 */
esp_err_t mesh_node_init(mesh_role_t role);

/**
 * @brief Configure router credentials (ROOT only, before start).
 *        For ROOT, these are obtained from the provisioning manager.
 *        For CHILD, not needed — the child connects to the mesh AP.
 */
esp_err_t mesh_node_set_router(const char *ssid, const char *password);

/**
 * @brief Start the mesh network.
 *        Blocks until the node joins the network (timeout ~10s).
 * @return ESP_OK on success.
 */
esp_err_t mesh_node_start(void);

/**
 * @brief Check if the mesh link is connected.
 */
bool mesh_node_is_connected(void);

/**
 * @brief Get current mesh link state.
 */
mesh_link_state_t mesh_node_get_state(void);

/**
 * @brief Send data to a mesh peer (or to root if addr is NULL).
 * @param to_mac  Destination MAC (NULL = send to root)
 * @param data    Payload data
 * @param data_len Payload length
 * @return ESP_OK or error.
 */
esp_err_t mesh_node_send(const uint8_t *to_mac, const uint8_t *data, int data_len);

/**
 * @brief Register a receive callback for mesh P2P data.
 */
void mesh_node_register_rx_cb(mesh_node_rx_cb_t cb);

/**
 * @brief Get the MAC address of the root node.
 *        Only valid after MESH_LINK_CONNECTED.
 */
esp_err_t mesh_node_get_root_mac(uint8_t mac[6]);

#endif /* MESH_NODE_H */
