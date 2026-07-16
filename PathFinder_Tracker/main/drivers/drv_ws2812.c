#include "drv_ws2812.h"
#include "tracker_config.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "drv_ws2812";

static led_strip_handle_t s_strip = NULL;

esp_err_t drv_ws2812_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812_GPIO,
        .max_leds = WS2812_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10 MHz – 0.1 us per tick */
        .mem_block_symbols = 0,             /* driver picks default */
        .flags = {
            .with_dma = false,
        }
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip device: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 initialised: %d LEDs on GPIO%d", WS2812_LED_COUNT, WS2812_GPIO);
    return ESP_OK;
}

void drv_ws2812_clear(void)
{
    if (s_strip) {
        led_strip_clear(s_strip);
    }
}

void drv_ws2812_set_led(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip && index < WS2812_LED_COUNT) {
        led_strip_set_pixel(s_strip, index, r, g, b);
    }
}

void drv_ws2812_show(void)
{
    if (s_strip) {
        led_strip_refresh(s_strip);
    }
}

void drv_ws2812_show_angle(float angle)
{
    if (!s_strip) {
        return;
    }

    if (angle < 0.0f || angle >= 360.0f) {
        led_strip_clear(s_strip);
        return;
    }

    uint32_t led_index = ((uint32_t)(angle / 10.0f)) % WS2812_LED_COUNT;

    led_strip_clear(s_strip);
    led_strip_set_pixel(s_strip, led_index, 255, 0, 0); /* red */
    led_strip_refresh(s_strip);
}
