#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

/**
 * @file ws2812.h
 * @brief WS2812/WS2812B 灯带控制接口。
 *
 * 本模块基于 ESP-IDF led_strip 组件和 RMT 外设驱动 36 颗 WS2812 LED，
 * 用于将声源方向角显示为环形灯带上的一个亮点。
 */

/**
 * @brief WS2812 数据线连接的 GPIO。
 */
#define LED_STRIP_GPIO_PIN  2

/**
 * @brief 是否启用 RMT DMA。
 *
 * 置 1 时使用 DMA 发送灯带波形，适合更多 LED 或更高刷新需求。
 * 当前灯数较少，默认关闭 DMA。
 *
 * @note RMT DMA 只在 ESP32-S3、ESP32-P4 等部分芯片上可用。
 */
#define LED_STRIP_USE_DMA  0

#if LED_STRIP_USE_DMA
/**
 * @brief 灯带 LED 数量。
 */
#define LED_STRIP_LED_COUNT 36

/**
 * @brief DMA 模式下 RMT 内存块大小。
 */
#define LED_STRIP_MEMORY_BLOCK_WORDS 1024
#else
/**
 * @brief 灯带 LED 数量。
 */
#define LED_STRIP_LED_COUNT 36

/**
 * @brief 非 DMA 模式下由驱动自动选择 RMT 内存块大小。
 */
#define LED_STRIP_MEMORY_BLOCK_WORDS 0
#endif // LED_STRIP_USE_DMA

/**
 * @brief RMT 分辨率。
 *
 * 10 MHz 表示 1 tick = 0.1 us，满足 WS2812 对高精度脉宽的要求。
 */
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

/**
 * @brief 初始化 WS2812 灯带驱动。
 *
 * 创建 led_strip 句柄并配置 RMT 后端。调用其他 WS2812 接口前必须先调用本函数。
 */
void ws2812_init(void);

/**
 * @brief 设置指定 LED 的 RGB 颜色。
 *
 * @param index LED 下标，范围为 0 到 LED_STRIP_LED_COUNT - 1。
 * @param red 红色亮度，范围 0~255。
 * @param green 绿色亮度，范围 0~255。
 * @param blue 蓝色亮度，范围 0~255。
 *
 * @note 本函数只写入驱动缓存，调用 ws2812_show() 后才会真正刷新到灯带。
 */
void ws2812_ctrl(uint32_t index, uint32_t red, uint32_t green, uint32_t blue);

/**
 * @brief 将当前 LED 缓存刷新到灯带。
 */
void ws2812_show(void);

/**
 * @brief 清空灯带显示。
 *
 * 将所有 LED 置灭，并更新驱动缓存。
 */
void ws2812_clear(void);

/**
 * @brief WS2812 闪烁测试函数。
 *
 * 循环点亮和熄灭全部 LED，用于验证灯带硬件和 RMT 输出是否正常。
 *
 * @note 本函数内部为无限循环，仅用于调试测试，不应在正常应用主流程中直接调用。
 */
void ws2812_test(void);
