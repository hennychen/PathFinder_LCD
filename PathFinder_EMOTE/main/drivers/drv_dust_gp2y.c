/**
 * @file drv_dust_gp2y.c
 * @brief Sharp GP2Y1010AU0F (DFRobot SEN0144) 粉尘传感器驱动实现
 *
 * 采样流程：
 *   1. LED=LOW (点亮 IR LED)
 *   2. 等待 280µs (光电晶体管响应稳定)
 *   3. ADC 读取
 *   4. 等待 40µs
 *   5. LED=HIGH (熄灭 IR LED)
 *   6. 等待 9680µs (脉冲间隔)
 *   以上重复 10 次取平均
 *
 * 浓度换算 (Chris Nafis 线性公式)：
 *   Vo >= 0.6V → density = 0.17 * Vo - 0.1  (mg/m³)
 *   Vo <  0.6V → density = 0 (干净空气基线)
 *
 * 参考: http://www.howmuchsnow.com/arduino/airquality/
 */
#include "drv_dust_gp2y.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_dust";

/* ── 采样时序常量 (微秒) ── */
#define DUST_SAMPLING_TIME_US   280
#define DUST_DELTA_TIME_US      40
#define DUST_SLEEP_TIME_US      9680

/* ── 过采样次数 ── */
#define DUST_OVERSAMPLE_COUNT   10

/* ── 模块状态 ── */
static adc_oneshot_unit_handle_t s_adc_handle   = NULL;
static adc_cali_handle_t         s_adc_cali_handle = NULL;
static adc_channel_t             s_adc_channel;
static gpio_num_t                s_led_gpio;
static bool                      s_initialized = false;

/* AQI 等级映射 */
static uint8_t density_to_aqi(float density_mgm3)
{
    if (density_mgm3 <  0.05f) return 0;   /* 优 */
    if (density_mgm3 <  0.10f) return 1;   /* 良 */
    if (density_mgm3 <  0.15f) return 2;   /* 轻度污染 */
    if (density_mgm3 <  0.25f) return 3;   /* 中度污染 */
    return 4;                              /* 重度污染 */
}

esp_err_t drv_dust_init(adc_oneshot_unit_handle_t adc_handle, adc_channel_t adc_ch, gpio_num_t led_gpio)
{
    if (s_initialized) return ESP_OK;

    /* 配置 LED 控制引脚为输出，默认 HIGH (LED 熄灭) */
    s_led_gpio = led_gpio;
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << led_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED GPIO%d 配置失败: %s", led_gpio, esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(led_gpio, 1);  /* 默认熄灭 */

    /* 使用外部传入的 ADC1 handle (与 UV 传感器共享) */
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC handle 为空，请先初始化 ADC1 单元");
        return ESP_ERR_INVALID_ARG;
    }
    s_adc_handle = adc_handle;

    /* 配置通道: 12bit, 12dB 衰减 */
    s_adc_channel = adc_ch;
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, adc_ch, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ADC 校准 (曲线拟合) */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = adc_ch,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC 校准成功 (curve fitting)");
    } else {
        ESP_LOGW(TAG, "ADC 校准失败(%s)，使用原始值换算", esp_err_to_name(ret));
        s_adc_cali_handle = NULL;
    }

    /* 丢弃前几次 ADC 读取 */
    int discard_raw = 0;
    for (int i = 0; i < 5; i++) {
        adc_oneshot_read(s_adc_handle, s_adc_channel, &discard_raw);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "粉尘传感器初始化完成 (LED=GPIO%d, ADC_CH=%d)", led_gpio, adc_ch);
    return ESP_OK;
}

esp_err_t drv_dust_read(dust_data_t *out)
{
    if (!s_initialized || !out) return ESP_ERR_INVALID_STATE;

    /* 10 次脉冲过采样 */
    int sum_raw = 0;
    int valid_count = 0;

    for (int i = 0; i < DUST_OVERSAMPLE_COUNT; i++) {
        /* LED 点亮 */
        gpio_set_level(s_led_gpio, 0);
        esp_rom_delay_us(DUST_SAMPLING_TIME_US);

        /* ADC 采样 */
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC 读取失败 #%d: %s", i, esp_err_to_name(ret));
            continue;
        }

        /* LED 熄灭 */
        esp_rom_delay_us(DUST_DELTA_TIME_US);
        gpio_set_level(s_led_gpio, 1);

        sum_raw += raw;
        valid_count++;

        /* 脉冲间隔 */
        esp_rom_delay_us(DUST_SLEEP_TIME_US);
    }

    if (valid_count == 0) {
        ESP_LOGE(TAG, "全部采样失败");
        return ESP_FAIL;
    }

    int avg_raw = sum_raw / valid_count;
    out->raw = (uint16_t)avg_raw;

    /* 校准电压换算 */
    int voltage_mv = 0;
    if (s_adc_cali_handle != NULL) {
        adc_cali_raw_to_voltage(s_adc_cali_handle, avg_raw, &voltage_mv);
    } else {
        voltage_mv = (int)((float)avg_raw * 3300.0f / 4095.0f);
    }
    out->voltage = (float)voltage_mv / 1000.0f;

    /* 浓度换算 (Chris Nafis 线性公式, 降低阈值以适应干净空气)
     * 原始公式阈值 0.6V，但干净空气 Vo≈0.3-0.5V 会返回 0
     * 改为始终计算密度，负值裁剪到 0 */
    out->density_mgm3 = 0.17f * out->voltage - 0.1f;
    if (out->density_mgm3 < 0.0f) out->density_mgm3 = 0.0f;

    /* AQI 等级 */
    out->aqi_level = density_to_aqi(out->density_mgm3);

    ESP_LOGI(TAG, "raw=%d → %dmV → %.3f mg/m³ (AQI %d)",
             avg_raw, voltage_mv, out->density_mgm3, out->aqi_level);

    return ESP_OK;
}
