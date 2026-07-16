#ifndef DRV_UART_COMM_H
#define DRV_UART_COMM_H

#include "esp_err.h"
#include "tracker_protocol.h"
#include "tracker_config.h"

esp_err_t drv_uart_init(void);
esp_err_t drv_uart_send_angle(float angle, uint8_t valid);
esp_err_t drv_uart_send_state(uint8_t state);
esp_err_t drv_uart_send_frame(tracker_cmd_t cmd, const uint8_t *data, uint8_t data_len);

#endif /* DRV_UART_COMM_H */
