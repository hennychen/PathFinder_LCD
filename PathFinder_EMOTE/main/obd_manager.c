/**
 * @file obd_manager.c
 * @brief 车辆 OBD-II 数据管理器实现 (IDF 6.0 esp_driver_twai node API)
 */
#include "obd_manager.h"
#include <string.h>
#include <stddef.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

static const char *TAG = "obd_mgr";

/* ── 全局状态（Kconfig 关闭时 getter 也需可用） ── */
static obd_snapshot_t    s_obd_snap;
static SemaphoreHandle_t s_obd_mux = NULL;

#if CONFIG_OBD_TWAI_ENABLE

#include "esp_twai.h"
#include "esp_twai_onchip.h"

/* ── 硬件配置 ── */
#define OBD_TWAI_TX_GPIO   GPIO_NUM_40   /* → SN65HVD230 CTX/D */
#define OBD_TWAI_RX_GPIO   GPIO_NUM_38   /* ← SN65HVD230 CRX/R */
#define OBD_TWAI_BITRATE   500000        /* ISO 15765-4: 500 kbps */

/* ── OBD-II 协议常量 (ISO 15765-4, 11-bit) ──
 *
 * 物理寻址 (0x7E0)：仅 ECM 响应，不影响 TCM/VDC/ABS 等其他 ECU
 * 历史问题：原用 0x7DF 功能寻址广播，导致所有 ECU 被迫响应诊断请求，
 *   在斯巴鲁傲虎等车型上触发 U 类通信故障码 */
#define OBD_REQ_ID         0x7E0         /* 物理寻址：仅请求 ECM */
#define OBD_RESP_ID_BASE   0x7E8         /* ECM 响应 ID */
#define OBD_RESP_ID_MASK   0x7F8         /* 高 8 位匹配，放开低 3 位 */
#define OBD_MODE_CURRENT   0x01          /* Mode 01: 当前数据 */
#define OBD_PID_SUPPORTED  0x00          /* PID 0x00: 支持列表（作探测帧） */

/* ── 任务/调度参数 ── */
#define OBD_STACK_SIZE     (4 * 1024)
#define OBD_TASK_PRIO      3
#define OBD_TASK_CORE      1             /* 绑 Core 1，TWAI 中断随创建核注册 */
#define OBD_RX_QUEUE_LEN   8
#define OBD_RESP_TIMEOUT_MS   100        /* 单次请求响应超时 */
#define OBD_OFFLINE_THRESHOLD 10         /* 连续超时次数 → 离线 */
#define OBD_PROBE_PERIOD_MS   3000       /* 离线后探测帧周期（3s，降低总线干扰） */
#define OBD_INTER_FRAME_MS    20         /* 帧间保护间隔，避免连续请求淹没总线 */
#define OBD_TX_FAIL_LIMIT     3          /* 连续 TX 失败上限 → 退避 */

/* ── RX 队列消息（ISR 只做帧拷贝投递，解析在任务侧） ── */
typedef struct {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[TWAI_FRAME_MAX_LEN];
} obd_rx_msg_t;

/* ── 表驱动 PID 调度 ── */
typedef struct {
    uint8_t  pid;          /* Mode 01 PID */
    uint16_t period_ms;    /* 轮询周期 */
    bool     two_bytes;    /* true: raw=256A+B, false: raw=A */
    float    scale;        /* value = raw*scale + offset */
    float    offset;
    float    ema_alpha;    /* EMA 系数，1.0 = 不滤波 */
    uint8_t  valid_bit;    /* 快照 valid_bits 对应位 */
    size_t   dest_offset;  /* 快照内 float 字段偏移 */
} obd_pid_entry_t;

/* 轮询频率安全设计：
 * - 总请求 ≈6 帧/s（原设计 ≈19 帧/s），总线负载率 <1%
 * - RPM/车速 2Hz 足够仪表显示刷新，不干扰 ECU 自身周期性通信 */
static const obd_pid_entry_t s_pid_table[] = {
    /*  pid   period  2B     scale        offset  ema   valid_bit          dest */
    { 0x0C,   500,  true,  1.0f / 4.0f,     0.0f, 0.3f, OBD_VALID_RPM,      offsetof(obd_snapshot_t, rpm)          },
    { 0x0D,   500,  false, 1.0f,            0.0f, 1.0f, OBD_VALID_SPEED,    offsetof(obd_snapshot_t, speed_kph)    },
    { 0x05,  2000,  false, 1.0f,          -40.0f, 1.0f, OBD_VALID_COOLANT,  offsetof(obd_snapshot_t, coolant_c)    },
    { 0x11,  1000,  false, 100.0f / 255.0f, 0.0f, 1.0f, OBD_VALID_THROTTLE, offsetof(obd_snapshot_t, throttle_pct) },
    { 0x42,  2000,  true,  1.0f / 1000.0f,  0.0f, 1.0f, OBD_VALID_BATT,     offsetof(obd_snapshot_t, batt_v)       },
    { 0x0F,  2000,  false, 1.0f,          -40.0f, 1.0f, OBD_VALID_INTAKE,   offsetof(obd_snapshot_t, intake_c)     },
};
#define OBD_PID_COUNT (sizeof(s_pid_table) / sizeof(s_pid_table[0]))

/* ── 模块内部状态（仅 obd_task 访问，无需加锁） ── */
static twai_node_handle_t s_twai_node = NULL;
static QueueHandle_t      s_rx_queue = NULL;
static volatile bool      s_bus_off = false;   /* ISR 置位，任务侧触发 recover */

/* ─────────────────────────────────────────────────────────
 *  TWAI ISR 回调 — 只做帧拷贝 + 队列投递，避免与 LCD DMA ISR 抢时
 * ───────────────────────────────────────────────────────── */
static bool obd_rx_done_cb(twai_node_handle_t handle,
                           const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    BaseType_t hp_woken = pdFALSE;
    uint8_t buf[TWAI_FRAME_MAX_LEN];
    twai_frame_t rx_frame = {
        .buffer     = buf,
        .buffer_len = sizeof(buf),
    };
    if (twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK) {
        /* 仅关心标准帧数据帧 */
        if (!rx_frame.header.ide && !rx_frame.header.rtr) {
            obd_rx_msg_t msg;
            msg.id  = rx_frame.header.id;
            msg.len = twaifd_dlc2len(rx_frame.header.dlc);
            if (msg.len > TWAI_FRAME_MAX_LEN) {
                msg.len = TWAI_FRAME_MAX_LEN;
            }
            memcpy(msg.data, buf, msg.len);
            xQueueSendFromISR(s_rx_queue, &msg, &hp_woken);
        }
    }
    return hp_woken == pdTRUE;
}

static bool obd_state_change_cb(twai_node_handle_t handle,
                                const twai_state_change_event_data_t *edata, void *user_ctx)
{
    if (edata->new_sta == TWAI_ERROR_BUS_OFF) {
        s_bus_off = true;   /* 任务侧调用 twai_node_recover() */
    }
    return false;
}

/* ─────────────────────────────────────────────────────────
 *  OBD 请求-响应（串行，同一时刻仅一个未决请求）
 * ───────────────────────────────────────────────────────── */

/* 发送 Mode 01 请求：ID=0x7DF {0x02, 0x01, PID, 0x55...}
 * payload/frame 为 static —— twai_node_transmit 是异步持针接口，
 * 硬件忙时帧指针入 tx_mount_queue 在 ISR 中解引用，栈变量会 use-after-scope。
 * 本任务串行发送，static 复用安全。 */
static uint8_t      s_tx_payload[8];
static twai_frame_t s_tx_frame = {
    .header     = { .id = OBD_REQ_ID, .dlc = 8 },
    .buffer     = s_tx_payload,
    .buffer_len = sizeof(s_tx_payload),
};

static esp_err_t obd_send_request(uint8_t pid)
{
    s_tx_payload[0] = 0x02;
    s_tx_payload[1] = OBD_MODE_CURRENT;
    s_tx_payload[2] = pid;
    memset(&s_tx_payload[3], 0x55, 5);
    return twai_node_transmit(s_twai_node, &s_tx_frame, 20);
}

/* 等待匹配响应 {len, 0x41, PID, A, B, ...}，返回 true 并输出 A/B */
static bool obd_wait_response(uint8_t pid, uint8_t *a_out, uint8_t *b_out)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)OBD_RESP_TIMEOUT_MS * 1000;
    obd_rx_msg_t msg;

    while (1) {
        int64_t remain_us = deadline - esp_timer_get_time();
        if (remain_us <= 0) {
            return false;
        }
        if (xQueueReceive(s_rx_queue, &msg, pdMS_TO_TICKS(remain_us / 1000 + 1)) != pdTRUE) {
            return false;
        }
        /* 单帧响应：data[0]=有效字节数, data[1]=0x41, data[2]=PID */
        if (msg.len >= 4 && msg.data[1] == (0x40 | OBD_MODE_CURRENT) && msg.data[2] == pid) {
            *a_out = msg.data[3];
            *b_out = (msg.len >= 5) ? msg.data[4] : 0;
            return true;
        }
        /* 其他 ECU 的无关响应 → 丢弃继续等 */
    }
}

/* 快照写入（加锁） */
static void obd_snap_update(const obd_pid_entry_t *entry, float value)
{
    xSemaphoreTake(s_obd_mux, portMAX_DELAY);
    float *dest = (float *)((uint8_t *)&s_obd_snap + entry->dest_offset);
    if (entry->ema_alpha < 1.0f && (s_obd_snap.valid_bits & entry->valid_bit)) {
        *dest = entry->ema_alpha * value + (1.0f - entry->ema_alpha) * (*dest);
    } else {
        *dest = value;
    }
    s_obd_snap.valid_bits |= entry->valid_bit;
    s_obd_snap.connected = true;
    s_obd_snap.timestamp_us = esp_timer_get_time();
    xSemaphoreGive(s_obd_mux);
}

/* 转入离线：清 valid_bits，connected=false */
static void obd_snap_set_offline(void)
{
    xSemaphoreTake(s_obd_mux, portMAX_DELAY);
    s_obd_snap.valid_bits = 0;
    s_obd_snap.connected = false;
    xSemaphoreGive(s_obd_mux);
}

/* ─────────────────────────────────────────────────────────
 *  TWAI 节点初始化（必须在 obd_task 内执行 → 中断注册到 Core 1）
 * ───────────────────────────────────────────────────────── */
static esp_err_t obd_twai_init(void)
{
    twai_onchip_node_config_t node_cfg = {
        .io_cfg = {
            .tx = OBD_TWAI_TX_GPIO,
            .rx = OBD_TWAI_RX_GPIO,
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .bit_timing = {
            .bitrate = OBD_TWAI_BITRATE,
        },
        .fail_retry_cnt = 2,  /* ESP32-S3 (SJA1000 类 HAL)：非 -1 值均降级为 single-shot，
                                 * 仲裁失败/出错时静默丢弃，对"未接模块"场景更安全 */
        .tx_queue_depth = 4,
    };
    esp_err_t err = twai_new_node_onchip(&node_cfg, &s_twai_node);
    if (err != ESP_OK) {
        return err;
    }

    twai_event_callbacks_t cbs = {
        .on_rx_done      = obd_rx_done_cb,
        .on_state_change = obd_state_change_cb,
    };
    err = twai_node_register_event_callbacks(s_twai_node, &cbs, NULL);
    if (err != ESP_OK) {
        goto fail;
    }

    /* 硬件过滤：只收 ECU 响应 0x7E8~0x7EF（需在 enable 前配置） */
    twai_mask_filter_config_t filter_cfg = {
        .id     = OBD_RESP_ID_BASE,
        .mask   = OBD_RESP_ID_MASK,
        .is_ext = false,
    };
    err = twai_node_config_mask_filter(s_twai_node, 0, &filter_cfg);
    if (err != ESP_OK) {
        goto fail;
    }

    err = twai_node_enable(s_twai_node);
    if (err != ESP_OK) {
        goto fail;
    }
    return ESP_OK;

fail:
    twai_node_delete(s_twai_node);
    s_twai_node = NULL;
    return err;
}

/* ─────────────────────────────────────────────────────────
 *  OBD 轮询任务 (Core 1)
 * ───────────────────────────────────────────────────────── */
static void obd_task(void *arg)
{
    /* TWAI 节点在本任务内创建 → 中断落在 Core 1 */
    esp_err_t err = obd_twai_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TWAI 初始化失败 (%s)，OBD 功能停用", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "obd_task 启动 @Core1, TWAI TX=GPIO%d RX=GPIO%d 500kbps",
             OBD_TWAI_TX_GPIO, OBD_TWAI_RX_GPIO);

    int64_t next_due_us[OBD_PID_COUNT] = { 0 };
    int     timeout_streak = 0;   /* 连续超时计数 */
    int     tx_fail_streak = 0;   /* 连续 TX 失败计数 */
    bool    online = false;       /* 本地在线状态（快照 connected 与之同步） */

    while (1) {
        /* bus-off 恢复（ISR 置位标志，任务侧处理） */
        if (s_bus_off) {
            s_bus_off = false;
            ESP_LOGW(TAG, "TWAI bus-off，尝试恢复（冷却 5s）");
            twai_node_recover(s_twai_node);
            obd_snap_set_offline();
            online = false;
            timeout_streak = 0;
            tx_fail_streak = 0;
            vTaskDelay(pdMS_TO_TICKS(5000));  /* bus-off 后强制冷却 5s，避免频繁错误帧 */
            continue;
        }

        if (!online) {
            /* ── 离线态：每 3s 发一次探测帧 (PID 0x00 支持列表) ── */
            uint8_t a, b;
            xQueueReset(s_rx_queue);
            if (obd_send_request(OBD_PID_SUPPORTED) == ESP_OK &&
                obd_wait_response(OBD_PID_SUPPORTED, &a, &b)) {
                ESP_LOGI(TAG, "检测到车辆 ECU，恢复在线轮询");
                online = true;
                timeout_streak = 0;
                tx_fail_streak = 0;
                memset(next_due_us, 0, sizeof(next_due_us));   /* 立即全表轮询 */
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(OBD_PROBE_PERIOD_MS));
            continue;
        }

        /* ── 在线态：表驱动串行轮询到期 PID ── */
        int64_t now = esp_timer_get_time();
        bool any_sent = false;
        for (size_t i = 0; i < OBD_PID_COUNT; i++) {
            if (now < next_due_us[i]) {
                continue;
            }
            const obd_pid_entry_t *entry = &s_pid_table[i];
            next_due_us[i] = now + (int64_t)entry->period_ms * 1000;
            any_sent = true;

            uint8_t a, b;
            xQueueReset(s_rx_queue);   /* 丢弃残留帧，保证请求-响应配对 */
            if (obd_send_request(entry->pid) != ESP_OK) {
                timeout_streak++;
                tx_fail_streak++;
                if (tx_fail_streak >= OBD_TX_FAIL_LIMIT) {
                    ESP_LOGW(TAG, "连续 %d 次 TX 失败，退避 1s", tx_fail_streak);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    tx_fail_streak = 0;
                    break;
                }
            } else {
                tx_fail_streak = 0;
                if (obd_wait_response(entry->pid, &a, &b)) {
                    float raw = entry->two_bytes ? (256.0f * a + b) : (float)a;
                    obd_snap_update(entry, raw * entry->scale + entry->offset);
                    timeout_streak = 0;
                } else {
                    timeout_streak++;
                }
            }

            if (timeout_streak >= OBD_OFFLINE_THRESHOLD) {
                ESP_LOGW(TAG, "连续 %d 次无响应，转入离线探测", timeout_streak);
                obd_snap_set_offline();
                online = false;
                break;
            }

            /* 帧间保护间隔：避免连续请求淹没 CAN 总线 */
            vTaskDelay(pdMS_TO_TICKS(OBD_INTER_FRAME_MS));
        }

        /* 无到期 PID 时小睡，避免空转（最短周期 500ms，50ms 粒度足够） */
        if (!any_sent) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

#endif /* CONFIG_OBD_TWAI_ENABLE */

/* ─────────────────────────────────────────────────────────
 *  公开接口
 * ───────────────────────────────────────────────────────── */
esp_err_t obd_manager_init(void)
{
    if (s_obd_mux == NULL) {
        s_obd_mux = xSemaphoreCreateMutex();
        if (s_obd_mux == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    memset(&s_obd_snap, 0, sizeof(s_obd_snap));

#if CONFIG_OBD_TWAI_ENABLE
    s_rx_queue = xQueueCreate(OBD_RX_QUEUE_LEN, sizeof(obd_rx_msg_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(obd_task, "obd", OBD_STACK_SIZE,
                                            NULL, OBD_TASK_PRIO, NULL, OBD_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "obd_task 创建失败");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OBD 管理器已初始化 (轮询 %u 个 PID)", (unsigned)OBD_PID_COUNT);
#else
    ESP_LOGI(TAG, "OBD_TWAI_ENABLE 未启用，OBD 保持离线");
#endif
    return ESP_OK;
}

esp_err_t obd_manager_get(obd_snapshot_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_obd_mux == NULL) {
        memset(out, 0, sizeof(*out));   /* 未初始化 → 离线空快照 */
        return ESP_OK;
    }
    xSemaphoreTake(s_obd_mux, portMAX_DELAY);
    memcpy(out, &s_obd_snap, sizeof(*out));
    xSemaphoreGive(s_obd_mux);
    return ESP_OK;
}
