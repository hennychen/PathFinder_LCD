/**
 * @file drv_es7210.c
 * @brief ES7210 four-channel audio ADC driver wrapper for PathFinder_Tracker.
 *
 * Initialises the I2C master bus, ES7210 codec (24-bit DSP-A TDM), and the
 * ESP32-S3 I2S0 RX channel.  Provides a simple read function that returns
 * de-interleaved, normalised float audio for all four microphone channels.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/i2s_tdm.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "es7210.h"
#include "tracker_config.h"
#include "drv_es7210.h"

static const char *TAG = "drv_es7210";

/* ---- ES7210 codec configuration constants ---- */
#define ES7210_MCLK_MULTIPLE    256
#define ES7210_MIC_BIAS_CFG     ES7210_MIC_BIAS_2V87
#define ES7210_MIC_GAIN_CFG     ES7210_MIC_GAIN_37_5DB
#define ES7210_ADC_VOLUME_DB    0

/* 24-bit normalisation: 2^23 */
#define NORM_FACTOR_24BIT       8388608.0f

/* ---- Module state ---- */
static i2c_master_bus_handle_t s_i2c_bus  = NULL;
static es7210_dev_handle_t     s_codec    = NULL;
static i2s_chan_handle_t       s_rx_chan  = NULL;
static bool                     s_ready    = false;

/* ----------------------------------------------------------------- */
/*  Public API                                                       */
/* ----------------------------------------------------------------- */

esp_err_t drv_es7210_init(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t err;

    /* PA_EN already pulled HIGH in app_main() board_power_enable(). */

    /* ---- I2C master bus ---- */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = ES7210_I2C_PORT,
        .sda_io_num             = ES7210_I2C_SDA_GPIO,
        .scl_io_num             = ES7210_I2C_SCL_GPIO,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .intr_priority          = 0,
        .trans_queue_depth      = 0,
        .flags.enable_internal_pullup = true,
    };

    err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* ---- 2. ES7210 codec ---- */
    es7210_i2c_config_t i2c_conf = {
        .i2c_bus_handle = s_i2c_bus,
        .i2c_addr       = ES7210_I2C_ADDR,
    };

    err = es7210_new_codec(&i2c_conf, &s_codec);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es7210_new_codec failed: %s", esp_err_to_name(err));
        return err;
    }

    es7210_codec_config_t codec_cfg = {
        .sample_rate_hz     = ES7210_SAMPLE_RATE,
        .mclk_ratio         = ES7210_MCLK_MULTIPLE,
        .i2s_format         = ES7210_I2S_FMT_DSP_A,
        .bit_width          = ES7210_I2S_BITS_24B,
        .mic_bias           = ES7210_MIC_BIAS_CFG,
        .mic_gain           = ES7210_MIC_GAIN_CFG,
        .flags.tdm_enable   = true,
    };

    err = es7210_config_codec(s_codec, &codec_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es7210_config_codec failed: %s", esp_err_to_name(err));
        return err;
    }

    err = es7210_config_volume(s_codec, ES7210_ADC_VOLUME_DB);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es7210_config_volume failed: %s", esp_err_to_name(err));
        return err;
    }

    /* ---- 3. I2S TDM RX channel ---- */
    i2s_chan_config_t chan_cfg = {
        .id             = ES7210_I2S_NUM,
        .role           = I2S_ROLE_MASTER,
        .dma_desc_num   = 4,
        .dma_frame_num  = ES7210_SAMPLE_SIZE,   /* 256 frames per DMA descriptor */
    };

    err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(ES7210_SAMPLE_RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {
            .mclk = ES7210_I2S_MCLK_GPIO,
            .bclk = ES7210_I2S_BCLK_GPIO,
            .ws   = ES7210_I2S_WS_GPIO,
            .din  = ES7210_I2S_DIN_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    /* 24-bit audio in 32-bit TDM slots */
    tdm_cfg.clk_cfg.mclk_multiple        = I2S_MCLK_MULTIPLE_256;
    tdm_cfg.slot_cfg.total_slot          = 4;
    tdm_cfg.slot_cfg.data_bit_width      = I2S_DATA_BIT_WIDTH_32BIT;
    tdm_cfg.slot_cfg.slot_bit_width      = I2S_SLOT_BIT_WIDTH_32BIT;

    err = i2s_channel_init_tdm_mode(s_rx_chan, &tdm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_tdm_mode failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "ES7210 ready: %d Hz, 24-bit, DSP-A TDM, 4-ch on I2S%d",
             ES7210_SAMPLE_RATE, (int)ES7210_I2S_NUM);
    return ESP_OK;
}

esp_err_t drv_es7210_read(float data_out[ES7210_CHANNELS][ES7210_SAMPLE_SIZE])
{
    if (!s_ready || s_rx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Each I2S frame contains 4 TDM slots x 4 bytes (32-bit) = 16 bytes.
     * 256 frames => 4096 bytes = 1024 int32_t words.
     */
    static int32_t raw_buf[ES7210_CHANNELS * ES7210_SAMPLE_SIZE];
    size_t  bytes_read = 0;

    esp_err_t err = i2s_channel_read(s_rx_chan,
                                     raw_buf,
                                     sizeof(raw_buf),
                                     &bytes_read,
                                     pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (bytes_read != sizeof(raw_buf)) {
        ESP_LOGW(TAG, "short read: %u / %u bytes",
                 (unsigned)bytes_read, (unsigned)sizeof(raw_buf));
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * De-interleave and normalise.
     *
     * ES7210 outputs 24-bit audio MSB-first in each 32-bit TDM slot,
     * so the sample occupies bits [31:8].  Arithmetic right-shift by 8
     * preserves the sign; division by 2^23 maps to [-1, 1).
     */
    for (int i = 0; i < ES7210_SAMPLE_SIZE; i++) {
        for (int ch = 0; ch < ES7210_CHANNELS; ch++) {
            int32_t raw       = raw_buf[i * ES7210_CHANNELS + ch];
            int32_t sample24  = raw >> 8;          /* sign-extended 24-bit value */
            data_out[ch][i]   = (float)sample24 / NORM_FACTOR_24BIT;
        }
    }

    return ESP_OK;
}

int drv_es7210_get_i2s_port(void)
{
    return (int)ES7210_I2S_NUM;
}
