/**
 * @file drv_uart_comm.c
 * @brief UART inter-board communication driver for PathFinder_Tracker.
 *
 * Sends tracking status (sound-source angle, track state) from Board B
 * (tracker) to Board A (PathFinder_EMOTE LCD display) over UART1.
 *
 * Frame layout (variable length):
 *   [HEADER 0xAA][CMD][LEN][DATA[LEN]][CRC8][TAIL 0x55]
 *
 * CRC8 covers CMD + LEN + DATA bytes (polynomial 0x07, init 0x00).
 */

#include <string.h>
#include "drv_uart_comm.h"
#include "esp_log.h"

static const char *TAG = "drv_uart";

static bool s_ready = false;

/* ----------------------------------------------------------------- */
/*  CRC8 – polynomial 0x07, init 0x00 (CRC-8/SMBUS variant)          */
/* ----------------------------------------------------------------- */

uint8_t tracker_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ----------------------------------------------------------------- */
/*  Public API                                                       */
/* ----------------------------------------------------------------- */

esp_err_t drv_uart_init(void)
{
    if (s_ready) return ESP_OK;

    uart_config_t uart_cfg = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(UART_PORT_NUM, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(UART_PORT_NUM, UART_TX_GPIO, UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE, UART_BUF_SIZE,
                              0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ready = true;
    ESP_LOGI(TAG, "UART comm ready: TX=GPIO%d, RX=GPIO%d @ %d baud",
             UART_TX_GPIO, UART_RX_GPIO, UART_BAUD_RATE);
    return ESP_OK;
}

esp_err_t drv_uart_send_frame(tracker_cmd_t cmd, const uint8_t *data, uint8_t data_len)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data_len > 16) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Assemble variable-length frame buffer.
       Max size: header(1) + cmd(1) + len(1) + data(16) + crc(1) + tail(1) = 21 */
    uint8_t buf[21];
    uint8_t idx = 0;

    buf[idx++] = UART_FRAME_HEADER;
    buf[idx++] = (uint8_t)cmd;
    buf[idx++] = data_len;
    if (data_len > 0 && data != NULL) {
        memcpy(&buf[idx], data, data_len);
        idx += data_len;
    }
    /* CRC8 over cmd + len + data */
    buf[idx++] = tracker_crc8(&buf[1], 2 + data_len);
    buf[idx++] = UART_FRAME_TAIL;

    int written = uart_write_bytes(UART_PORT_NUM, buf, idx);
    if (written < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t drv_uart_send_angle(float angle, uint8_t valid)
{
    /* Pack angle as fixed-point uint16 (0.1 deg resolution), little-endian,
       followed by a 1-byte valid flag. */
    uint16_t angle_fixed = (uint16_t)(angle * 10.0f);

    uint8_t data[3];
    data[0] = (uint8_t)(angle_fixed & 0xFF);         /* low byte  */
    data[1] = (uint8_t)((angle_fixed >> 8) & 0xFF);  /* high byte */
    data[2] = valid;

    return drv_uart_send_frame(CMD_ANGLE_DATA, data, sizeof(data));
}

esp_err_t drv_uart_send_state(uint8_t state)
{
    return drv_uart_send_frame(CMD_TRACK_STATE, &state, 1);
}
