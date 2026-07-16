#ifndef DRV_WS2812_H
#define DRV_WS2812_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t drv_ws2812_init(void);
void drv_ws2812_clear(void);
void drv_ws2812_set_led(uint32_t index, uint8_t r, uint8_t g, uint8_t b);
void drv_ws2812_show(void);
void drv_ws2812_show_angle(float angle);

#endif /* DRV_WS2812_H */
