/*
 * es7210_audio_codec.cc
 *
 * Bridges ES7210 4-channel TDM ADC with xiaozhi's int16_t audio interface.
 *
 * Speaker path (inherited):  xiaozhi int16_t → NoAudioCodec::Write → I2S0 STD → MAX98357A
 * Microphone path (override): ES7210 TDM 4-ch → I2S1 TDM RX → Read() extracts CH0 → xiaozhi int16_t
 */

#include "es7210_audio_codec.h"
#include "es7210.h"
#include "config.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_check.h>
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include <driver/i2s_std.h>
#include <cstring>
#include <cmath>
#include <vector>

#define TAG "Es7210AudioCodec"

/* ES7210 codec configuration constants (matches PathFinder_Tracker drv_es7210.c) */
#define ES7210_MCLK_MULTIPLE    256
#define ES7210_MIC_BIAS_CFG     ES7210_MIC_BIAS_2V87
#define ES7210_MIC_GAIN_CFG     ES7210_MIC_GAIN_0DB   /* Diagnostic: was 30DB, testing if RMS scales with gain */
#define ES7210_ADC_VOLUME_DB    0

/* ----------------------------------------------------------------- */
/*  Constructor                                                      */
/* ----------------------------------------------------------------- */

Es7210AudioCodec::Es7210AudioCodec(
    int input_sample_rate, int output_sample_rate,
    gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
    gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din, gpio_num_t mic_mclk,
    gpio_num_t i2c_sda, gpio_num_t i2c_scl, uint8_t i2c_addr)
{
    duplex_ = false;
    input_sample_rate_  = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // 1. Speaker TX channel: I2S0 STD mode for MAX98357A
    InitSpeakerChannel(spk_bclk, spk_ws, spk_dout);

    // 2. ES7210 codec via I2C (must precede TDM – ES7210 needs config before clock)
    InitEs7210Codec(i2c_sda, i2c_scl, i2c_addr);

    // 3. Microphone RX channel: I2S1 TDM mode for ES7210 4-ch
    InitTdmRxChannel(mic_sck, mic_ws, mic_din, mic_mclk);

    ESP_LOGI(TAG, "ES7210 AudioCodec ready: in=%d Hz, out=%d Hz, TDM 4-ch",
             input_sample_rate_, output_sample_rate_);

    // 4. SPEAKER SELF-TEST + GPIO line diagnostic.
    //
    // Plays 3 beeps AND samples GPIO1/2/3 (BCLK/LRCK/DOUT) levels during playback
    // to verify I2S1 is actually driving these pins.  Also reads GPIO45 (PA_EN)
    // to confirm the power-enable line is still HIGH.
    ESP_LOGW(TAG, "*** SPEAKER SELF-TEST START: 3 beeps @1kHz, amp=0.7 ***");
    
    // Pre-check: GPIO matrix state for SPK pins
    ESP_LOGW(TAG, "PRE-CHECK GPIO levels: BCLK(1)=%d LRCK(2)=%d DOUT(3)=%d PA_EN(45)=%d",
             gpio_get_level(GPIO_NUM_1), gpio_get_level(GPIO_NUM_2),
             gpio_get_level(GPIO_NUM_3), gpio_get_level(GPIO_NUM_45));
    
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    const int beep_ms = 200;
    const int beep_samples = output_sample_rate_ * beep_ms / 1000;
    std::vector<int32_t> sine_buf(beep_samples);
    const double AMP = 0.7 * 2147483647.0;  // 0.7 * INT32_MAX
    for (int i = 0; i < beep_samples; i++) {
        double t = (double)i / output_sample_rate_;
        double s = AMP * sin(2.0 * 3.14159265 * 1000.0 * t);
        sine_buf[i] = (int32_t)s;
    }
    for (int beep = 0; beep < 3; beep++) {
        size_t written = 0;
        esp_err_t werr = i2s_channel_write(tx_handle_, sine_buf.data(),
                                           beep_samples * sizeof(int32_t),
                                           &written, portMAX_DELAY);
        // Sample GPIO levels mid-playback (multiple reads to catch toggling)
        int b1 = gpio_get_level(GPIO_NUM_1);
        int b2 = gpio_get_level(GPIO_NUM_2);
        int b3 = gpio_get_level(GPIO_NUM_3);
        int b1b = gpio_get_level(GPIO_NUM_1);
        int b3b = gpio_get_level(GPIO_NUM_3);
        ESP_LOGW(TAG, "  beep %d: write ret=%s, bytes=%d (expected %d)",
                 beep + 1, esp_err_to_name(werr), (int)written,
                 (int)(beep_samples * sizeof(int32_t)));
        ESP_LOGW(TAG, "    DURING: BCLK(1)=%d->%d LRCK(2)=%d DOUT(3)=%d->%d  PA_EN(45)=%d",
                 b1, b1b, b2, b3, b3b, gpio_get_level(GPIO_NUM_45));
        vTaskDelay(pdMS_TO_TICKS(150));  // 150ms gap between beeps
    }
    ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    
    // Post-disable check: pins should revert to default state
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGW(TAG, "POST-CHECK GPIO levels: BCLK(1)=%d LRCK(2)=%d DOUT(3)=%d PA_EN(45)=%d",
             gpio_get_level(GPIO_NUM_1), gpio_get_level(GPIO_NUM_2),
             gpio_get_level(GPIO_NUM_3), gpio_get_level(GPIO_NUM_45));
    ESP_LOGW(TAG, "*** SPEAKER SELF-TEST COMPLETE — did you hear 3 beeps? ***");
}

Es7210AudioCodec::~Es7210AudioCodec()
{
    if (es7210_dev_) {
        es7210_del_codec(es7210_dev_);
        es7210_dev_ = nullptr;
    }
    // i2c_bus_ is intentionally not freed — the bus lives for app lifetime.
    // rx_handle_ / tx_handle_ are freed by NoAudioCodec destructor.
}

/* ----------------------------------------------------------------- */
/*  Speaker TX (identical to NoAudioCodecSimplex speaker half)       */
/* ----------------------------------------------------------------- */

void Es7210AudioCodec::InitSpeakerChannel(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout)
{
    i2s_chan_config_t chan_cfg = {
        .id = XIAOZHI_I2S_PORT(1),
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
#endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Speaker TX: I2S1 STD %d Hz", output_sample_rate_);
}

/* ----------------------------------------------------------------- */
/*  ES7210 codec I2C init + register configuration                   */
/* ----------------------------------------------------------------- */

void Es7210AudioCodec::InitEs7210Codec(gpio_num_t sda, gpio_num_t scl, uint8_t addr)
{
    /* I2C master bus on port 1 (camera SCCB uses port 0) */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = (i2c_port_t)ES7210_I2C_PORT,
        .sda_io_num             = sda,
        .scl_io_num             = scl,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .intr_priority          = 0,
        .trans_queue_depth      = 0,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus_));

    /* Create ES7210 device handle */
    es7210_i2c_config_t i2c_conf = {
        .i2c_bus_handle = i2c_bus_,
        .i2c_addr       = addr,
    };
    ESP_ERROR_CHECK(es7210_new_codec(&i2c_conf, &es7210_dev_));

    /* Configure ES7210: DSP-A TDM, 24-bit, sample_rate from config */
    es7210_codec_config_t codec_cfg = {
        .sample_rate_hz     = (uint32_t)input_sample_rate_,
        .mclk_ratio         = ES7210_MCLK_MULTIPLE,
        .i2s_format         = ES7210_I2S_FMT_DSP_A,
        .bit_width          = ES7210_I2S_BITS_24B,
        .mic_bias           = ES7210_MIC_BIAS_CFG,
        .mic_gain           = ES7210_MIC_GAIN_CFG,
        .flags = { .tdm_enable = 1 },
    };
    ESP_ERROR_CHECK(es7210_config_codec(es7210_dev_, &codec_cfg));
    ESP_ERROR_CHECK(es7210_config_volume(es7210_dev_, ES7210_ADC_VOLUME_DB));

    ESP_LOGI(TAG, "ES7210 codec configured: %d Hz, DSP-A TDM, addr 0x%02X on I2C%d",
             input_sample_rate_, addr, ES7210_I2C_PORT);
}

/* ----------------------------------------------------------------- */
/*  ES7210 TDM RX channel on I2S port 1                              */
/* ----------------------------------------------------------------- */

void Es7210AudioCodec::InitTdmRxChannel(gpio_num_t sck, gpio_num_t ws, gpio_num_t din, gpio_num_t mclk)
{
    /* RX-only channel on I2S0 (original AcousticEye uses I2S0 for TDM, verified working).
     * DMA: 4 descriptors × 256 frames — matches original drv_es7210.c exactly. */
    i2s_chan_config_t chan_cfg = {
        .id = XIAOZHI_I2S_PORT(0),
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 256,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));

    /* TDM Philips/DSP-A: 4 slots × 16-bit */
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG((uint32_t)input_sample_rate_),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3)),
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = sck,
            .ws   = ws,
            .dout = I2S_GPIO_UNUSED,
            .din  = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    /* 16-bit audio in 16-bit TDM slots (matches original AcousticEye driver) */
    tdm_cfg.clk_cfg.mclk_multiple   = I2S_MCLK_MULTIPLE_256;
    tdm_cfg.slot_cfg.total_slot     = ES7210_TDM_CHANNELS;
    tdm_cfg.slot_cfg.data_bit_width  = I2S_DATA_BIT_WIDTH_16BIT;
    tdm_cfg.slot_cfg.slot_bit_width  = I2S_SLOT_BIT_WIDTH_16BIT;

    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &tdm_cfg));
    ESP_LOGI(TAG, "TDM RX: I2S0 %d Hz, %d-ch 16-bit", input_sample_rate_, ES7210_TDM_CHANNELS);
}

/* ----------------------------------------------------------------- */
/*  Speaker diagnostics: trace EnableOutput / Write path             */
/* ----------------------------------------------------------------- */

void Es7210AudioCodec::EnableOutput(bool enable)
{
    ESP_LOGW(TAG, "<<< EnableOutput(%s) called, current output_enabled_=%d",
             enable ? "true" : "false", (int)output_enabled_);
    NoAudioCodec::EnableOutput(enable);
    ESP_LOGW(TAG, "<<< EnableOutput done, output_enabled_=%d tx_handle=%p",
             (int)output_enabled_, tx_handle_);
}

int Es7210AudioCodec::Write(const int16_t* data, int samples)
{
    static int s_write_count = 0;
    s_write_count++;
    /* Print first 3 calls and then every 200th call (~ at speak boundaries) */
    if (s_write_count <= 3 || s_write_count % 200 == 0) {
        int32_t sum = 0;
        int32_t abs_max = 0;
        for (int i = 0; i < samples && i < 256; i++) {
            int16_t v = data[i];
            sum += v;
            int32_t a = (v < 0) ? -v : v;
            if (a > abs_max) abs_max = a;
        }
        int32_t avg = sum / (samples > 256 ? 256 : samples);
        ESP_LOGW(TAG, ">>> Write #%d samples=%d vol=%d avg=%d absmax=%d first=%d",
                 s_write_count, samples, output_volume_, avg, abs_max, data[0]);
    }
    return NoAudioCodec::Write(data, samples);
}

/* ----------------------------------------------------------------- */
/*  Read: extract CH0 from 4-channel TDM stream                      */
/* ----------------------------------------------------------------- */

int Es7210AudioCodec::Read(int16_t* dest, int samples)
{
    if (samples <= 0) return 0;

    /* TDM buffer: 4 interleaved channels per sample frame */
    const int tdm_total = samples * ES7210_TDM_CHANNELS;
    std::vector<int16_t> tdm_buf(tdm_total);

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_handle_, tdm_buf.data(),
                                     tdm_total * sizeof(int16_t),
                                     &bytes_read, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TDM read failed: %s", esp_err_to_name(err));
        return 0;
    }

    int frames_read = bytes_read / (sizeof(int16_t) * ES7210_TDM_CHANNELS);
    if (frames_read <= 0) return 0;

    /* ===== Diagnostic: 4-channel statistics BEFORE any processing ===== */
    static int s_diag_count = 0;
    s_diag_count++;
    if (s_diag_count % 50 == 0 && frames_read > 0) {  /* ~0.5s interval */
        int32_t  sum[4]    = {0,0,0,0};
        int64_t  sum_sq[4] = {0,0,0,0};
        int16_t  vmin[4]   = {INT16_MAX,INT16_MAX,INT16_MAX,INT16_MAX};
        int16_t  vmax[4]   = {INT16_MIN,INT16_MIN,INT16_MIN,INT16_MIN};
        int32_t  zero_cross[4] = {0,0,0,0};
        int16_t  prev[4]   = {0,0,0,0};
        for (int i = 0; i < frames_read; i++) {
            for (int ch = 0; ch < 4; ch++) {
                int16_t v = tdm_buf[i * 4 + ch];
                sum[ch]    += v;
                sum_sq[ch] += (int64_t)v * v;
                if (v < vmin[ch]) vmin[ch] = v;
                if (v > vmax[ch]) vmax[ch] = v;
                if (i > 0 && ((prev[ch] < 0 && v >= 0) || (prev[ch] >= 0 && v < 0)))
                    zero_cross[ch]++;
                prev[ch] = v;
            }
        }
        ESP_LOGI(TAG, "=== TDM diagnostic (n=%d, count=%d) ===", frames_read, s_diag_count);
        for (int ch = 0; ch < 4; ch++) {
            int32_t mean = sum[ch] / frames_read;
            int32_t rms  = (int32_t)sqrtf((float)(sum_sq[ch] / frames_read));
            ESP_LOGI(TAG, "  CH%d: mean=%7d rms=%6d min=%6d max=%6d zcr=%d",
                     ch, mean, rms, vmin[ch], vmax[ch], zero_cross[ch]);
        }
    }

    /* De-interleave: extract slot 0 (CH0) for xiaozhi voice path */
    for (int i = 0; i < frames_read; i++) {
        dest[i] = tdm_buf[i * ES7210_TDM_CHANNELS + 0];
    }

    /* DC blocker: removes DC offset.
     * Filter: y[n] = x[n] - x[n-1] + R * y[n-1]  (R=0.995) */
    for (int i = 0; i < frames_read; i++) {
        float x = (float)dest[i];
        float y = x - dc_prev_in_ + DC_R * dc_prev_out_;
        dc_prev_in_ = x;
        dc_prev_out_ = y;
        int32_t val = (int32_t)y;
        dest[i] = (val > INT16_MAX) ? INT16_MAX :
                  (val < INT16_MIN) ? INT16_MIN : (int16_t)val;
    }

    return frames_read;
}
