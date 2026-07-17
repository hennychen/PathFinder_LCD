/**
 * @file comm_link.c
 * @brief Unified communication dispatcher implementation.
 *
 * Priority: ESP-NOW > Mesh P2P > UART backup
 */
#include <string.h>
#include "comm_link.h"
#include "mesh_espnow.h"
#include "mesh_node.h"
#include "mesh_protocol.h"
#include "drv_uart_comm.h"
#include "esp_log.h"

static const char *TAG = "comm_link";

static uint8_t s_root_mac[6] = {0};
static bool    s_root_mac_known = false;
static uint8_t s_espnow_fail_count = 0;
static bool    s_initialized = false;

#define ESPNOW_FAIL_THRESHOLD 5

esp_err_t comm_link_init(void)
{
    /* Try to get root MAC from mesh */
    if (mesh_node_get_root_mac(s_root_mac) == ESP_OK) {
        /* Check if it's non-zero */
        bool nonzero = false;
        for (int i = 0; i < 6; i++) {
            if (s_root_mac[i] != 0) { nonzero = true; break; }
        }
        if (nonzero) {
            s_root_mac_known = true;
            /* Pre-register as ESP-NOW peer */
            mesh_espnow_add_peer(s_root_mac);
            ESP_LOGI(TAG, "Root MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                     s_root_mac[0], s_root_mac[1], s_root_mac[2],
                     s_root_mac[3], s_root_mac[4], s_root_mac[5]);
        }
    }
    s_initialized = true;
    ESP_LOGI(TAG, "Comm link initialized (root_known=%d)", s_root_mac_known);
    return ESP_OK;
}

/* ── Internal: try ESP-NOW first ── */
static esp_err_t try_espnow(uint8_t msg_type, const uint8_t *data, uint8_t len)
{
    if (!mesh_espnow_is_ready() || !s_root_mac_known) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = mesh_espnow_send(s_root_mac, msg_type, data, len);
    if (ret == ESP_OK) {
        s_espnow_fail_count = 0;
        return ESP_OK;
    }

    /* ESP-NOW failed — increment counter */
    if (++s_espnow_fail_count >= ESPNOW_FAIL_THRESHOLD) {
        ESP_LOGW(TAG, "ESP-NOW failed %d times, will try Mesh fallback", s_espnow_fail_count);
    }
    return ESP_FAIL;
}

/* ── Internal: try Mesh P2P second ── */
static esp_err_t try_mesh(const uint8_t *data, int len)
{
    if (!mesh_node_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Send to root (NULL addr) */
    return mesh_node_send(NULL, data, len);
}

/* ── Pack angle as u16 fixed-point + valid ── */
static void pack_angle(float angle, uint8_t valid, uint8_t *out, uint8_t *out_len)
{
    uint16_t angle_fixed = (uint16_t)(angle * 10.0f);
    out[0] = (uint8_t)(angle_fixed & 0xFF);
    out[1] = (uint8_t)((angle_fixed >> 8) & 0xFF);
    out[2] = valid;
    *out_len = 3;
}

esp_err_t comm_link_send_angle(float angle, uint8_t valid)
{
    if (!s_initialized) {
        /* Not initialized yet — direct UART */
        return drv_uart_send_angle(angle, valid);
    }

    uint8_t payload[3];
    uint8_t payload_len;
    pack_angle(angle, valid, payload, &payload_len);

    /* Serialize for mesh fallback (includes type/seq/crc) */
    uint8_t mesh_buf[ESPNOW_MAX_PAYLOAD + 4];
    int mesh_total = mesh_msg_serialize(mesh_buf, MSG_ANGLE_DATA, 0,
                                         payload, payload_len);

    /* Priority 1: ESP-NOW */
    if (try_espnow(MSG_ANGLE_DATA, payload, payload_len) == ESP_OK) {
        return ESP_OK;
    }

    /* Priority 2: Mesh P2P */
    if (mesh_total > 0 && try_mesh(mesh_buf, mesh_total) == ESP_OK) {
        return ESP_OK;
    }

    /* Priority 3: UART backup */
    return drv_uart_send_angle(angle, valid);
}

esp_err_t comm_link_send_state(uint8_t state)
{
    if (!s_initialized) {
        return drv_uart_send_state(state);
    }

    uint8_t mesh_buf[ESPNOW_MAX_PAYLOAD + 4];
    int mesh_total = mesh_msg_serialize(mesh_buf, MSG_TRACK_STATE, 0,
                                         &state, 1);

    /* Priority 1: ESP-NOW */
    if (try_espnow(MSG_TRACK_STATE, &state, 1) == ESP_OK) {
        return ESP_OK;
    }

    /* Priority 2: Mesh P2P */
    if (mesh_total > 0 && try_mesh(mesh_buf, mesh_total) == ESP_OK) {
        return ESP_OK;
    }

    /* Priority 3: UART backup */
    return drv_uart_send_state(state);
}

esp_err_t comm_link_send_face_info(uint8_t face_found, int16_t cx, int16_t cy,
                                     uint16_t w, uint16_t h)
{
    /* Pack face info: found(1) + cx(2) + cy(2) + w(2) + h(2) = 9 bytes */
    uint8_t payload[9];
    payload[0] = face_found;
    payload[1] = (uint8_t)(cx & 0xFF);
    payload[2] = (uint8_t)((cx >> 8) & 0xFF);
    payload[3] = (uint8_t)(cy & 0xFF);
    payload[4] = (uint8_t)((cy >> 8) & 0xFF);
    payload[5] = (uint8_t)(w & 0xFF);
    payload[6] = (uint8_t)((w >> 8) & 0xFF);
    payload[7] = (uint8_t)(h & 0xFF);
    payload[8] = (uint8_t)((h >> 8) & 0xFF);

    /* Also send via UART for backward compat */
    drv_uart_send_frame(CMD_FACE_INFO, payload, sizeof(payload));

    /* Try wireless */
    if (!s_initialized) return ESP_OK;

    /* Priority 1: ESP-NOW */
    if (try_espnow(MSG_FACE_INFO, payload, sizeof(payload)) == ESP_OK) {
        return ESP_OK;
    }

    /* Priority 2: Mesh P2P */
    uint8_t mesh_buf[ESPNOW_MAX_PAYLOAD + 4];
    int mesh_total = mesh_msg_serialize(mesh_buf, MSG_FACE_INFO, 0,
                                         payload, sizeof(payload));
    if (mesh_total > 0 && try_mesh(mesh_buf, mesh_total) == ESP_OK) {
        return ESP_OK;
    }

    return ESP_OK;  /* UART already sent above */
}

esp_err_t comm_link_send_heartbeat(void)
{
    if (!s_initialized) return ESP_OK;

    uint8_t hb_data = 0;  /* Heartbeat payload can be empty or carry a counter */
    return try_espnow(MSG_HEARTBEAT, &hb_data, 1);
}

bool comm_link_wireless_ready(void)
{
    return mesh_espnow_is_ready() || mesh_node_is_connected();
}
