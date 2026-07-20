/**
 * @file mesh_node.c
 * @brief ESP-WIFI-MESH node manager implementation.
 *
 * Handles mesh initialization, configuration, event handling, and data
 * send/receive. Both ROOT (Board A) and CHILD (Board B) use this module.
 */
#include <string.h>
#include "mesh_node.h"
#include "mesh_protocol.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "mesh_node";

/* ── Internal state ── */
static mesh_role_t       s_role = MESH_ROLE_CHILD;
static mesh_link_state_t s_state = MESH_LINK_IDLE;
static mesh_node_rx_cb_t s_rx_cb = NULL;
static uint8_t           s_root_mac[6] = {0};
static char              s_router_ssid[33] = {0};
static char              s_router_pass[65] = {0};
static bool              s_mesh_started = false;
static bool              s_is_root = false;

/* Event group for mesh events */
static EventGroupHandle_t s_mesh_events = NULL;
#define BIT_PARENT_CONNECTED  (1 << 0)
#define BIT_ROOT_GOT_IP       (1 << 1)

/* ── Mesh event handler ── */
static void mesh_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0};
    static int  last_layer = 0;

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(TAG, "MESH_STARTED, id: %02x:%02x:%02x:%02x:%02x:%02x",
                 id.addr[0], id.addr[1], id.addr[2],
                 id.addr[3], id.addr[4], id.addr[5]);
        s_state = MESH_LINK_CONNECTING;
        break;
    }

    case MESH_EVENT_STOPPED: {
        ESP_LOGI(TAG, "MESH_STOPPED");
        s_state = MESH_LINK_DISCONNECTED;
        xEventGroupClearBits(s_mesh_events, BIT_PARENT_CONNECTED);
        break;
    }

    case MESH_EVENT_CHILD_CONNECTED: {
        /* We (as parent) got a child connection */
        ESP_LOGI(TAG, "MESH_CHILD_CONNECTED");
        break;
    }

    case MESH_EVENT_CHILD_DISCONNECTED: {
        ESP_LOGW(TAG, "MESH_CHILD_DISCONNECTED");
        break;
    }

    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *route_table =
            (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGI(TAG, "MESH_ROUTING_TABLE_ADD: +%d (total: %d)",
                 route_table->rt_size_change, route_table->rt_size_new);
        break;
    }

    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *route_table =
            (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGI(TAG, "MESH_ROUTING_TABLE_REMOVE: -%d (total: %d)",
                 route_table->rt_size_change, route_table->rt_size_new);
        break;
    }

    case MESH_EVENT_PARENT_CONNECTED: {
        ESP_LOGI(TAG, "MESH_PARENT_CONNECTED");
        s_state = MESH_LINK_CONNECTED;
        xEventGroupSetBits(s_mesh_events, BIT_PARENT_CONNECTED);

        /* Store root MAC */
        mesh_addr_t parent;
        if (esp_mesh_get_parent_bssid(&parent) == ESP_OK) {
            /* If we're root, our own MAC is root; else parent's parent... is root.
               For a 2-layer mesh, parent IS root for child nodes. */
            if (esp_mesh_get_layer() == MESH_ROOT_LAYER) {
                esp_wifi_get_mac(WIFI_IF_STA, s_root_mac); /* We are root */
            } else {
                /* Parent is root in a 2-layer mesh */
                memcpy(s_root_mac, parent.addr, 6);
            }
        }

        if (!s_is_root) {
            /* Start ESP-NOW now that we have a Wi-Fi connection via mesh */
        }
        break;
    }

    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected =
            (mesh_event_disconnected_t *)event_data;
        ESP_LOGW(TAG, "MESH_PARENT_DISCONNECTED, reason:%d",
                 disconnected->reason);
        s_state = MESH_LINK_DISCONNECTED;
        xEventGroupClearBits(s_mesh_events, BIT_PARENT_CONNECTED);
        break;
    }

    case MESH_EVENT_NO_PARENT_FOUND: {
        ESP_LOGW(TAG, "MESH_NO_PARENT_FOUND");
        s_state = MESH_LINK_CONNECTING;
        break;
    }

    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change =
            (mesh_event_layer_change_t *)event_data;
        ESP_LOGI(TAG, "MESH_LAYER_CHANGE, layer:%d->%d",
                 last_layer, layer_change->new_layer);
        last_layer = layer_change->new_layer;
        s_is_root = (layer_change->new_layer == MESH_ROOT_LAYER);
        break;
    }

    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs =
            (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(TAG, "MESH_TODS_REACHABLE: %s",
                 (*toDs == MESH_TODS_REACHABLE) ? "reachable" : "unreachable");
        break;
    }

    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr =
            (mesh_event_root_address_t *)event_data;
        memcpy(s_root_mac, root_addr->addr, 6);
        ESP_LOGI(TAG, "MESH_ROOT_ADDRESS: %02x:%02x:%02x:%02x:%02x:%02x",
                 s_root_mac[0], s_root_mac[1], s_root_mac[2],
                 s_root_mac[3], s_root_mac[4], s_root_mac[5]);
        break;
    }

    case MESH_EVENT_ROOT_SWITCH_REQ: {
        ESP_LOGI(TAG, "MESH_ROOT_SWITCH_REQ");
        break;
    }

    case MESH_EVENT_ROOT_SWITCH_ACK: {
        ESP_LOGI(TAG, "MESH_ROOT_SWITCH_ACK");
        break;
    }

    case MESH_EVENT_ROOT_ASKED_YIELD: {
        ESP_LOGI(TAG, "MESH_ROOT_ASKED_YIELD");
        break;
    }

    default:
        ESP_LOGD(TAG, "MESH event: %d", (int)event_id);
        break;
    }
}

/* ── IP event handler (root node gets IP from router) ── */
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "ROOT got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_mesh_events, BIT_ROOT_GOT_IP);
    }
}

/* ── Mesh RX task ── */
static void mesh_rx_task(void *arg)
{
    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;
    uint8_t rx_buf[1500];  /* MESH_MTU = 1500 */

    ESP_LOGI(TAG, "Mesh RX task started");

    while (1) {
        data.data = rx_buf;
        data.size = sizeof(rx_buf);

        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err == ESP_OK && data.size > 0) {
            ESP_LOGD(TAG, "Mesh RX: %d bytes", data.size);

            if (s_rx_cb) {
                s_rx_cb(from.addr, data.data, data.size);
            }
        } else if (err != ESP_ERR_MESH_TIMEOUT) {
            ESP_LOGW(TAG, "esp_mesh_recv error: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════ */

esp_err_t mesh_node_init(mesh_role_t role)
{
    s_role = role;

    /* Create event group */
    if (!s_mesh_events) {
        s_mesh_events = xEventGroupCreate();
    }

    /* Initialize LwIP (every node must init, even non-root) */
    esp_err_t netif_ret = esp_netif_init();
    if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(netif_ret));
        return netif_ret;
    }

    /* Create default event loop if not already created */
    esp_err_t loop_ret = esp_event_loop_create_default();
    if (loop_ret != ESP_OK && loop_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s",
                 esp_err_to_name(loop_ret));
        return loop_ret;
    }

    /* Create default netif for STA (required by mesh) */
    esp_netif_create_default_wifi_sta();
    /* Mesh also needs AP netif for the mesh AP interface */
    esp_netif_create_default_wifi_ap();

    /* Wi-Fi initialization */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID,
                                                &mesh_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &ip_event_handler, NULL));

    /* Initialize mesh */
    ESP_ERROR_CHECK(esp_mesh_init());

    /* Configure mesh */
    mesh_cfg_t mesh_cfg = MESH_INIT_CONFIG_DEFAULT();
    uint8_t mesh_id[6] = MESH_ID_BYTES;
    memcpy((uint8_t *)&mesh_cfg.mesh_id, mesh_id, 6);
    mesh_cfg.channel = MESH_CHANNEL;

    /* Router config (for root node to connect to router) */
    if (strlen(s_router_ssid) > 0) {
        mesh_cfg.router.ssid_len = strlen(s_router_ssid);
        memcpy((uint8_t *)&mesh_cfg.router.ssid, s_router_ssid,
               mesh_cfg.router.ssid_len);
        memcpy((uint8_t *)&mesh_cfg.router.password, s_router_pass,
               strlen(s_router_pass));
    }

    /* Mesh AP config */
    mesh_cfg.mesh_ap.max_connection = MESH_AP_CONNECTIONS;
    memcpy((uint8_t *)&mesh_cfg.mesh_ap.password, MESH_AP_PASSWD,
           strlen(MESH_AP_PASSWD));

    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_cfg));

    /* Set max layer */
    esp_mesh_set_max_layer(MESH_MAX_LAYER);

    /* Set fixed root if ROOT role (avoids election delays) */
    if (role == MESH_ROLE_ROOT) {
        esp_mesh_fix_root(true);
        ESP_LOGI(TAG, "Configured as ROOT node");
    } else {
        esp_mesh_fix_root(false);
        ESP_LOGI(TAG, "Configured as CHILD node");
    }

    ESP_LOGI(TAG, "Mesh initialized (role=%s)",
             role == MESH_ROLE_ROOT ? "ROOT" : "CHILD");
    return ESP_OK;
}

esp_err_t mesh_node_set_router(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    strncpy(s_router_ssid, ssid, sizeof(s_router_ssid) - 1);
    if (password) {
        strncpy(s_router_pass, password, sizeof(s_router_pass) - 1);
    }
    ESP_LOGI(TAG, "Router credentials set: SSID='%s'", s_router_ssid);
    return ESP_OK;
}

esp_err_t mesh_node_start(void)
{
    if (s_mesh_started) {
        ESP_LOGW(TAG, "Mesh already started");
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_mesh_start());
    s_mesh_started = true;

    /* Start Wi-Fi (mesh_start does this internally, but we need to be sure
       ESP-NOW can use the Wi-Fi driver) */
    ESP_LOGI(TAG, "Mesh started, waiting for parent connection...");

    /* Start mesh RX task */
    xTaskCreate(mesh_rx_task, "mesh_rx", 4096, NULL, 5, NULL);

    return ESP_OK;
}

bool mesh_node_is_connected(void)
{
    return s_state == MESH_LINK_CONNECTED;
}

mesh_link_state_t mesh_node_get_state(void)
{
    return s_state;
}

esp_err_t mesh_node_send(const uint8_t *to_mac, const uint8_t *data, int data_len)
{
    if (!s_mesh_started || s_state != MESH_LINK_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || data_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    mesh_data_t mdata;
    mdata.data = (uint8_t *)data;
    mdata.size = data_len;
    mdata.proto = MESH_PROTO_BIN;
    mdata.tos = MESH_TOS_P2P;

    int flag = MESH_DATA_P2P;
    if (to_mac == NULL) {
        /* Send to root */
        flag = MESH_DATA_TODS;
    }

    return esp_mesh_send((mesh_addr_t *)to_mac, &mdata, flag, NULL, 0);
}

void mesh_node_register_rx_cb(mesh_node_rx_cb_t cb)
{
    s_rx_cb = cb;
}

esp_err_t mesh_node_get_root_mac(uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    memcpy(mac, s_root_mac, 6);
    return ESP_OK;
}

/* ════════════════════════════════════════════════
 *  ROOT node entry: starts mesh on top of existing WiFi
 *  (used by Board A after wifi_config_manager provisioning)
 * ════════════════════════════════════════════════ */

esp_err_t mesh_node_start_root_after_wifi(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "start_root_after_wifi: missing SSID");
        return ESP_ERR_INVALID_ARG;
    }

    s_role = MESH_ROLE_ROOT;

    /* Create event group if not already */
    if (!s_mesh_events) {
        s_mesh_events = xEventGroupCreate();
    }

    /* Register mesh + IP event handlers (WiFi/netif already initialized
       by wifi_config_manager, so we only register mesh events here) */
    esp_err_t ret = esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID,
                                                &mesh_event_handler, NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "register mesh event handler failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &ip_event_handler, NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "register IP event handler failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize mesh stack (on top of existing WiFi) */
    ret = esp_mesh_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_mesh_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure mesh */
    mesh_cfg_t mesh_cfg = MESH_INIT_CONFIG_DEFAULT();
    uint8_t mesh_id[6] = MESH_ID_BYTES;
    memcpy((uint8_t *)&mesh_cfg.mesh_id, mesh_id, 6);
    mesh_cfg.channel = MESH_CHANNEL;

    /* Router config */
    mesh_cfg.router.ssid_len = strlen(ssid);
    memcpy((uint8_t *)&mesh_cfg.router.ssid, ssid, mesh_cfg.router.ssid_len);
    if (password) {
        memcpy((uint8_t *)&mesh_cfg.router.password, password, strlen(password));
    }

    /* Mesh AP config */
    mesh_cfg.mesh_ap.max_connection = MESH_AP_CONNECTIONS;
    memcpy((uint8_t *)&mesh_cfg.mesh_ap.password, MESH_AP_PASSWD,
           strlen(MESH_AP_PASSWD));

    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_cfg));

    /* Set as ROOT (fixed) */
    esp_mesh_set_max_layer(MESH_MAX_LAYER);
    esp_mesh_fix_root(true);

    /* Start mesh — 容错处理：B板(xiaozhi)已是 ROOT 时会失败，不应 abort */
    ret = esp_mesh_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_mesh_start() failed: %s — 另一板可能已是 ROOT，继续运行(非致命)",
                 esp_err_to_name(ret));
        esp_mesh_deinit();
        return ret;
    }
    s_mesh_started = true;
    ESP_LOGI(TAG, "Mesh ROOT started after WiFi provisioning (router='%s')", ssid);

    /* Start mesh RX task */
    xTaskCreate(mesh_rx_task, "mesh_rx", 4096, NULL, 5, NULL);

    return ESP_OK;
}
