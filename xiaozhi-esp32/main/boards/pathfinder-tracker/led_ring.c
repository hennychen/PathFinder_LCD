/**
 * @file led_ring.c
 * @brief WS2812 36-LED ring driver — sound source direction indicator.
 *
 * Uses espressif__led_strip RMT backend to drive 36 WS2812 LEDs on GPIO48.
 * Angle → LED: index = (angle / 10) % 36
 */

#include "led_ring.h"
#include "config.h"

#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "LedRing"

#define LED_RING_COUNT      WS2812_RING_COUNT
#define LED_AUTO_OFF_MS     1500   /* 无声音 1.5s 后熄灭 */

static led_strip_handle_t s_strip   = NULL;
static int64_t             s_last_us = 0;
static int                 s_last_idx = -1;

void led_ring_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = WS2812_RING_GPIO,
        .max_leds       = WS2812_RING_COUNT,
        .led_model      = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,   /* 10 MHz */
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(ret));
        s_strip = NULL;
        return;
    }

    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 ring init OK: %d LEDs on GPIO%d", WS2812_RING_COUNT, WS2812_RING_GPIO);
}

void led_ring_show_angle(float angle)
{
    if (!s_strip) return;

    if (angle < 0.0f) {
        /* 无效角度 — 如果超过超时时间则熄灭 */
        if (s_last_idx >= 0 && s_last_us > 0) {
            int64_t now = esp_timer_get_time();
            if ((now - s_last_us) > LED_AUTO_OFF_MS * 1000) {
                led_strip_clear(s_strip);
                s_last_idx = -1;
            }
        }
        return;
    }

    /* angle 0~360 → LED 0~35 */
    int idx = (int)(angle / 10.0f) % LED_RING_COUNT;
    if (idx < 0) idx += LED_RING_COUNT;

    if (idx == s_last_idx) {
        /* 同一颗 LED，只刷新时间戳 */
        s_last_us = esp_timer_get_time();
        return;
    }

    /* 清除所有，只亮目标 LED（红色） */
    led_strip_clear(s_strip);
    led_strip_set_pixel(s_strip, idx, 255, 0, 0);  /* R=255, G=0, B=0 */
    led_strip_refresh(s_strip);

    s_last_idx = idx;
    s_last_us  = esp_timer_get_time();

    ESP_LOGI(TAG, "LED[%d] angle=%.1f deg", idx, angle);
}
