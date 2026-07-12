/**
 * @file ble_gatt_server.c
 * @brief BLE GATT Server 实现 — 基于 NimBLE 协议栈
 *
 * 二进制数据格式与 Flutter App 解码器严格对齐:
 *   C2 环境 20B: [magic4][temp_i16][humi_u16][press_u32][alt_i16][uv_u16][pad4]
 *   C3 运动 8B:  [pitch_i16][roll_i16][accel_u16][evt_u8][conf_u8]
 *   C4 表情 15B: [id_u8][len_u8][name..12][trigger_u8]
 */
#include "ble_gatt_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>

static const char *TAG = "ble_gatt";

/* ── UUID 定义 ── */
/* 这些是标准蓝牙 16-bit UUID (Base UUID 扩展)，必须用 BLE_UUID16 */
/* Service: 0xFE00 */
static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0xFE00);

/* C2 Env: 0xFE02 */
static const ble_uuid16_t c2_env_uuid = BLE_UUID16_INIT(0xFE02);

/* C3 Motion: 0xFE03 */
static const ble_uuid16_t c3_motion_uuid = BLE_UUID16_INIT(0xFE03);

/* C4 Emote: 0xFE04 */
static const ble_uuid16_t c4_emote_uuid = BLE_UUID16_INIT(0xFE04);

/* C5 WiFi: 0xFE05 (Write + Notify) */
static const ble_uuid16_t c5_wifi_uuid = BLE_UUID16_INIT(0xFE05);

/* ── 连接状态 ── */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_initialized = false;

/* ── 特征值 handle ── */
static uint16_t s_c2_handle = 0;
static uint16_t s_c3_handle = 0;
static uint16_t s_c4_handle = 0;
static uint16_t s_c5_handle = 0;

/* ── CCCD 订阅状态 ── */
static bool s_c2_subscribed = false;
static bool s_c3_subscribed = false;
static bool s_c4_subscribed = false;
static bool s_c5_subscribed = false;

/* ── C5 WiFi Write 回调 + JSON 分包缓冲 ── */
static ble_wifi_write_cb_t s_wifi_write_cb = NULL;
#define WIFI_JSON_BUF_SIZE 512
static char s_json_buf[WIFI_JSON_BUF_SIZE];
static int  s_json_buf_pos = 0;

/* ════════════════════════════════════════════════════════════
 *  GATT 特征值访问回调
 * ════════════════════════════════════════════════════════════ */

static int gatt_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* 读请求 — 返回当前值（简化处理） */
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t dummy[20] = {0};
        os_mbuf_append(ctxt->om, dummy, sizeof(dummy));
        return 0;
    }

    /* C5 Write — 接收配网 JSON */
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (attr_handle == s_c5_handle) {
            uint8_t data[200];
            uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
            if (data_len > sizeof(data) - 1) data_len = sizeof(data) - 1;
            ble_hs_mbuf_to_flat(ctxt->om, data, data_len, &data_len);
            data[data_len] = '\0';

            /* 分包处理: 首字节 0x00=完整帧开始, 0x01=分片后续 */
            if (data[0] == 0x00) {
                /* 新帧开始 */
                s_json_buf_pos = 0;
                size_t copy_len = data_len - 1;
                if (copy_len >= WIFI_JSON_BUF_SIZE) copy_len = WIFI_JSON_BUF_SIZE - 1;
                memcpy(s_json_buf, &data[1], copy_len);
                s_json_buf_pos = copy_len;
            } else if (data[0] == 0x01) {
                /* 分片后续 */
                size_t copy_len = data_len - 1;
                if (s_json_buf_pos + copy_len >= WIFI_JSON_BUF_SIZE) {
                    copy_len = WIFI_JSON_BUF_SIZE - 1 - s_json_buf_pos;
                }
                memcpy(&s_json_buf[s_json_buf_pos], &data[1], copy_len);
                s_json_buf_pos += copy_len;
            }

            s_json_buf[s_json_buf_pos] = '\0';

            /* 检查 JSON 是否完整 (闭合 }) */
            if (strchr(s_json_buf, '}') != NULL) {
                ESP_LOGI(TAG, "收到完整 JSON: %s", s_json_buf);
                if (s_wifi_write_cb) {
                    s_wifi_write_cb(s_json_buf);
                }
                s_json_buf_pos = 0;
            }
            return 0;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/* ════════════════════════════════════════════════════════════
 *  GATT 服务定义
 * ════════════════════════════════════════════════════════════ */

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &c2_env_uuid.u,
                .access_cb = gatt_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_c2_handle,
            },
            {
                .uuid = &c3_motion_uuid.u,
                .access_cb = gatt_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_c3_handle,
            },
            {
                .uuid = &c4_emote_uuid.u,
                .access_cb = gatt_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_c4_handle,
            },
            {
                .uuid = &c5_wifi_uuid.u,
                .access_cb = gatt_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_c5_handle,
            },
            {
                0, /* No more characteristics */
            }
        },
    },
    {
        0, /* No more services */
    },
};

/* 前向声明 */
void ble_gatt_server_start_adv(void);

/* ════════════════════════════════════════════════════════════
 *  GAP 事件回调（连接/断开）
 * ════════════════════════════════════════════════════════════ */

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "客户端已连接 handle=%d", s_conn_handle);
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGW(TAG, "连接失败 status=%d", event->connect.status);
            /* 重新广播 */
            ble_gatt_server_start_adv();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "客户端断开 reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_c2_subscribed = false;
        s_c3_subscribed = false;
        s_c4_subscribed = false;
        s_c5_subscribed = false;
        s_json_buf_pos = 0;
        /* 重新广播 */
        ble_gatt_server_start_adv();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* NimBLE 原生订阅事件 */
        if (event->subscribe.cur_notify == 1) {
            /* 客户端开启了 notify */
            if (event->subscribe.attr_handle == s_c2_handle) {
                s_c2_subscribed = true;
                ESP_LOGI(TAG, "C2(Env) subscribed");
            } else if (event->subscribe.attr_handle == s_c3_handle) {
                s_c3_subscribed = true;
                ESP_LOGI(TAG, "C3(Motion) subscribed");
            } else if (event->subscribe.attr_handle == s_c4_handle) {
                s_c4_subscribed = true;
                ESP_LOGI(TAG, "C4(Emote) subscribed");
            } else if (event->subscribe.attr_handle == s_c5_handle) {
                s_c5_subscribed = true;
                ESP_LOGI(TAG, "C5(WiFi) subscribed");
            }
        } else if (event->subscribe.cur_notify == 0) {
            /* 客户端取消了 notify */
            if (event->subscribe.attr_handle == s_c2_handle) {
                s_c2_subscribed = false;
                ESP_LOGI(TAG, "C2(Env) unsubscribed");
            } else if (event->subscribe.attr_handle == s_c3_handle) {
                s_c3_subscribed = false;
                ESP_LOGI(TAG, "C3(Motion) unsubscribed");
            } else if (event->subscribe.attr_handle == s_c4_handle) {
                s_c4_subscribed = false;
                ESP_LOGI(TAG, "C4(Emote) unsubscribed");
            } else if (event->subscribe.attr_handle == s_c5_handle) {
                s_c5_subscribed = false;
                ESP_LOGI(TAG, "C5(WiFi) unsubscribed");
            }
        }
        break;

    default:
        break;
    }
    return 0;
}

/* ════════════════════════════════════════════════════════════
 *  开始广播
 * ════════════════════════════════════════════════════════════ */

void ble_gatt_server_start_adv(void)
{
    struct ble_hs_adv_fields adv_fields;
    memset(&adv_fields, 0, sizeof(adv_fields));

    /* Flags: LE general discoverable + BR/EDR not supported */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 设备名 */
    const char *name = ble_svc_gap_device_name();
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    /* 16-bit Service UUID (0xFE00) */
    static const ble_uuid16_t svc_uuid16 = BLE_UUID16_INIT(0xFE00);
    adv_fields.uuids16 = (ble_uuid16_t *)&svc_uuid16;
    adv_fields.num_uuids16 = 1;
    adv_fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    /* 开始广播 */
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE 广播已启动: %s", name);
    }
}

/* ════════════════════════════════════════════════════════════
 *  NimBLE 同步回调 — controller ready 后开始广播
 * ════════════════════════════════════════════════════════════ */

static void on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE sync — 开始广播");
    ble_gatt_server_start_adv();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
}

/* ════════════════════════════════════════════════════════════
 *  NimBLE Host 任务
 * ════════════════════════════════════════════════════════════ */

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();  /* This function returns only on deinit */
    nimble_port_freertos_deinit();
}

/* ════════════════════════════════════════════════════════════
 *  初始化
 * ════════════════════════════════════════════════════════════ */

esp_err_t ble_gatt_server_init(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t ret;

    /* 1. 初始化 NimBLE port */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 配置 host */
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* 3. 初始化 GAP/GATT 服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* 4. 注册自定义 GATT 服务 */
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    /* 5. 设置设备名 */
    ble_svc_gap_device_name_set("PathFinder-EMOTE");

    /* 6. 启动 NimBLE host 任务 */
    nimble_port_freertos_init(nimble_host_task);

    s_initialized = true;
    ESP_LOGI(TAG, "BLE GATT Server 初始化完成");
    return ESP_OK;
}

/* ════════════════════════════════════════════════════════════
 *  数据推送 — 组装二进制帧并通知客户端
 * ════════════════════════════════════════════════════════════ */

/* 小端写入辅助 */
static inline void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}
static inline void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

void ble_gatt_notify_env(int16_t temp_x100, uint16_t humi_x100,
                         uint32_t pressure_pa, int16_t alt_x10,
                         uint16_t uv_x100)
{
    if (!s_initialized || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;
    if (!s_c2_subscribed)
        return;

    /* C2 帧: 20 bytes */
    uint8_t buf[20];
    memset(buf, 0, sizeof(buf));

    /* [0-3] magic header: "ENV\0" */
    buf[0] = 'E'; buf[1] = 'N'; buf[2] = 'V';
    /* [4-5] temperature × 100 (int16 LE) */
    put_u16le(&buf[4], (uint16_t)temp_x100);
    /* [6-7] humidity × 100 (uint16 LE) */
    put_u16le(&buf[6], humi_x100);
    /* [8-11] pressure (uint32 LE) */
    put_u32le(&buf[8], pressure_pa);
    /* [12-13] altitude × 10 (int16 LE) */
    put_u16le(&buf[12], (uint16_t)alt_x10);
    /* [14-15] uv × 100 (uint16 LE) */
    put_u16le(&buf[14], uv_x100);
    /* [16-19] padding */

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (om) {
        ble_gattc_notify_custom(s_conn_handle, s_c2_handle, om);
    }
}

void ble_gatt_notify_motion(int16_t pitch_x100, int16_t roll_x100,
                            uint16_t accel_x1000, uint8_t event,
                            uint8_t confidence)
{
    if (!s_initialized || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;
    if (!s_c3_subscribed)
        return;

    /* C3 帧: 8 bytes */
    uint8_t buf[8];
    put_u16le(&buf[0], (uint16_t)pitch_x100);
    put_u16le(&buf[2], (uint16_t)roll_x100);
    put_u16le(&buf[4], accel_x1000);
    buf[6] = event;
    buf[7] = confidence;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (om) {
        ble_gattc_notify_custom(s_conn_handle, s_c3_handle, om);
    }
}

void ble_gatt_notify_emote(uint8_t emote_id, const char *name,
                           uint8_t trigger)
{
    if (!s_initialized || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;
    if (!s_c4_subscribed)
        return;

    /* C4 帧: 15 bytes */
    uint8_t buf[15];
    memset(buf, 0, sizeof(buf));

    buf[0] = emote_id;
    /* 名称最多 12 字节 (byte[2..13] = 12 bytes, byte[14]=trigger) */
    size_t name_len = strlen(name);
    if (name_len > 12) name_len = 12;
    buf[1] = (uint8_t)name_len;
    memcpy(&buf[2], name, name_len);
    buf[14] = trigger;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (om) {
        ble_gattc_notify_custom(s_conn_handle, s_c4_handle, om);
    }
}

bool ble_gatt_is_connected(void)
{
    return s_initialized && s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

/* ════════════════════════════════════════════════════════════
 *  C5 WiFi 配网 — Notify + 回调注册
 * ════════════════════════════════════════════════════════════ */

void ble_gatt_notify_wifi_status(const char *json_str)
{
    if (!s_initialized || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;
    if (!s_c5_subscribed)
        return;

    size_t len = strlen(json_str);
    if (len > 180) len = 180;

    struct os_mbuf *om = ble_hs_mbuf_from_flat((const uint8_t *)json_str, len);
    if (om) {
        ble_gattc_notify_custom(s_conn_handle, s_c5_handle, om);
    }
}

void ble_gatt_register_wifi_write_cb(ble_wifi_write_cb_t cb)
{
    s_wifi_write_cb = cb;
}
