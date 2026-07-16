/**
 * @file mesh_espnow.c
 * @brief ESP-NOW communication layer implementation.
 *
 * Uses esp_now for low-latency P2P data transfer between Board A and Board B.
 * ESP-NOW packets are limited to 250 bytes (max user payload).
 */
#include <string.h>
#include "mesh_espnow.h"
#include "mesh_protocol.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "mesh_espnow";

const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool s_espnow_ready = false;
static SemaphoreHandle_t s_send_mutex = NULL;
static mesh_espnow_rx_cb_t s_rx_cb = NULL;
static uint8_t s_local_mac[6] = {0};
static uint8_t s_send_seq = 0;

/* ── ESP-NOW send callback ── */
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "Send to %02x:%02x:%02x:%02x:%02x:%02x failed",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    }
}

/* ── ESP-NOW receive callback (runs in Wi-Fi task context) ── */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                            const uint8_t *data, int data_len)
{
    if (!recv_info || !data || data_len < 4) {
        return;
    }

    /* Parse the message to verify CRC */
    mesh_msg_t msg;
    if (!mesh_msg_parse(data, data_len, &msg)) {
        ESP_LOGD(TAG, "CRC check failed for ESPNOW packet");
        return;
    }

    /* Forward valid message to application callback */
    if (s_rx_cb) {
        s_rx_cb(recv_info->src_addr, data, data_len);
    }
}

esp_err_t mesh_espnow_init(void)
{
    if (s_espnow_ready) {
        return ESP_OK;
    }

    /* Get local MAC */
    esp_wifi_get_mac(ESP_IF_WIFI_AP, s_local_mac);
    ESP_LOGI(TAG, "Local MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             s_local_mac[0], s_local_mac[1], s_local_mac[2],
             s_local_mac[3], s_local_mac[4], s_local_mac[5]);

    /* Initialize ESP-NOW */
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register callbacks */
    ret = esp_now_register_send_cb(espnow_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register_send_cb failed: %s", esp_err_to_name(ret));
        esp_now_deinit();
        return ret;
    }

    ret = esp_now_register_recv_cb(espnow_recv_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register_recv_cb failed: %s", esp_err_to_name(ret));
        esp_now_deinit();
        return ret;
    }

    /* Create send mutex for thread safety */
    s_send_mutex = xSemaphoreCreateMutex();
    if (!s_send_mutex) {
        ESP_LOGE(TAG, "Failed to create send mutex");
        esp_now_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_espnow_ready = true;
    ESP_LOGI(TAG, "ESP-NOW initialized successfully");
    return ESP_OK;
}

void mesh_espnow_register_rx_cb(mesh_espnow_rx_cb_t cb)
{
    s_rx_cb = cb;
}

esp_err_t mesh_espnow_add_peer(const uint8_t *mac)
{
    if (!s_espnow_ready || !mac) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if peer already exists */
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = 0;  /* 0 = current Wi-Fi channel */
    peer.ifidx = ESP_IF_WIFI_AP;
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac, 6);

    esp_err_t ret = esp_now_add_peer(&peer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add_peer failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t mesh_espnow_send(const uint8_t *target_mac, uint8_t msg_type,
                            const uint8_t *data, uint8_t data_len)
{
    if (!s_espnow_ready || !target_mac) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data_len > ESPNOW_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Ensure peer is registered */
    if (!esp_now_is_peer_exist(target_mac)) {
        esp_err_t ret = mesh_espnow_add_peer(target_mac);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Serialize message */
    uint8_t buf[ESPNOW_MAX_PAYLOAD + 4];
    int total = mesh_msg_serialize(buf, msg_type, s_send_seq++, data, data_len);
    if (total == 0) {
        xSemaphoreGive(s_send_mutex);
        return ESP_FAIL;
    }

    esp_err_t ret = esp_now_send(target_mac, buf, total);

    xSemaphoreGive(s_send_mutex);
    return ret;
}

bool mesh_espnow_is_ready(void)
{
    return s_espnow_ready;
}

esp_err_t mesh_espnow_get_local_mac(uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    memcpy(mac, s_local_mac, 6);
    return ESP_OK;
}
