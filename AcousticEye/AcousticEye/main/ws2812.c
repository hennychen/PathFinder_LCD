#include "ws2812.h"

/**
 * @file ws2812.c
 * @brief WS2812 灯带驱动实现。
 *
 * 本文件封装 ESP-IDF led_strip 组件，向应用层提供初始化、设置像素、
 * 刷新显示和清屏接口。
 */

#define TAG   "ws2812"

/**
 * @brief led_strip 组件返回的灯带操作句柄。
 */
led_strip_handle_t led_strip = NULL;

/**
 * @brief 初始化 WS2812 灯带和 RMT 发送后端。
 */
void ws2812_init(void)
{ 
    /** @brief 灯带基础配置，包括数据 GPIO、LED 数量、型号和颜色顺序。 */
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    /** @brief RMT 后端配置，包括时钟源、分辨率、内存块和 DMA 选项。 */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .mem_block_symbols = LED_STRIP_MEMORY_BLOCK_WORDS,
        .flags = {
            .with_dma = LED_STRIP_USE_DMA,
        }
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");

    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 5, 5, 205));
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "LED ON!");
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "LED OFF!");
}

/**
 * @brief 设置单颗 LED 的 RGB 颜色。
 *
 * @param index LED 下标。
 * @param red 红色分量。
 * @param green 绿色分量。
 * @param blue 蓝色分量。
 */
void ws2812_ctrl(uint32_t index, uint32_t red, uint32_t green, uint32_t blue)
{
    led_strip_set_pixel(led_strip, index, red, green, blue);
}

/**
 * @brief 刷新灯带，将缓存中的颜色数据发送到 WS2812。
 */
void ws2812_show(void)
{
    led_strip_refresh(led_strip);
}

/**
 * @brief 清空灯带缓存并熄灭所有 LED。
 */
void ws2812_clear(void)
{
    led_strip_clear(led_strip);
}

/**
 * @brief 灯带硬件测试函数。
 *
 * 以 500 ms 周期交替点亮和熄灭全部 LED，便于确认电源、数据线和 RMT
 * 输出是否正常。
 */
void ws2812_test(void)
{
    if(led_strip == NULL){
        ESP_LOGE(TAG, "LED strip object is null");
        return;
    }
    bool led_on_off = false;

    ESP_LOGI(TAG, "Start blinking LED strip");
    while (1) {
        if (led_on_off) {
            /** @brief 低亮度点亮全部 LED，避免测试时电流过大。 */
            for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 5, 5, 5));
            }
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
            ESP_LOGI(TAG, "LED ON!");
        } else {
            /** @brief 熄灭全部 LED。 */
            ESP_ERROR_CHECK(led_strip_clear(led_strip));
            ESP_LOGI(TAG, "LED OFF!");
        }

        led_on_off = !led_on_off;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
