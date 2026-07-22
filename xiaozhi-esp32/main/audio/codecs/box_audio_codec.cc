#include "box_audio_codec.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <driver/i2s_tdm.h>
#include <cmath>
#include <vector>

#define TAG "BoxAudioCodec"

BoxAudioCodec::BoxAudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin, uint8_t es8311_addr, uint8_t es7210_addr, bool input_reference,
    int tdm_channels) {
    duplex_ = true; // 是否双工
    input_reference_ = input_reference; // 是否使用参考输入，实现回声消除
    input_channels_ = input_reference_ ? 2 : 1; // 输入通道数
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = 37.5; // ES7210 PGA 最大增益，提升弱信号 GCC-PHAT 精度
    tdm_channels_ = tdm_channels;  // 0 = standard I2S, 4 = TDM 4-ch

    if (tdm_channels_ >= 4) {
        CreateTdmDuplexChannels(mclk, bclk, ws, dout, din);
    } else {
        CreateDuplexChannels(mclk, bclk, ws, dout, din);
    }

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Output
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)1,
        .addr = es8311_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if_ != NULL);

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = out_ctrl_if_;
    es8311_cfg.gpio_if = gpio_if_;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    es8311_cfg.pa_pin = pa_pin;
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    out_codec_if_ = es8311_codec_new(&es8311_cfg);
    assert(out_codec_if_ != NULL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(output_dev_ != NULL);

    // Input
    i2c_cfg.addr = es7210_addr;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(in_ctrl_if_ != NULL);

    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if_;
    // TDM mode: select all 4 mics (ES7210 driver auto-switches to TDM at >= 3 mics)
    // Standard mode: select only MIC1+MIC2 for shared BCLK consistency
    if (tdm_channels_ >= 4) {
        es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 |
                                   ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    } else {
        es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2;
    }
    in_codec_if_ = es7210_codec_new(&es7210_cfg);
    assert(in_codec_if_ != NULL);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(input_dev_ != NULL);

    ESP_LOGI(TAG, "BoxAudioDevice initialized");
}

BoxAudioCodec::~BoxAudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);

    audio_codec_delete_codec_if(in_codec_if_);
    audio_codec_delete_ctrl_if(in_ctrl_if_);
    audio_codec_delete_codec_if(out_codec_if_);
    audio_codec_delete_ctrl_if(out_ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

void BoxAudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    // TX: Standard I2S for ES8311 DAC
    // RX: Standard I2S for ES7210 ADC (2 mics → standard I2S, not TDM)
    // Both use standard I2S @ 48kHz → shared BCLK = 48000 × 32 = 1.536 MHz
    
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
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

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = I2S_GPIO_UNUSED,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &rx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    ESP_LOGI(TAG, "Duplex channels created (standard I2S)");
}

void BoxAudioCodec::CreateTdmDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    /* Create TX+RX channel pair on I2S0 (shared controller) */
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    /* TX: Standard I2S for ES8311 DAC (unchanged — ES8311 is not TDM-aware).
     * BCLK = 24000 × 2 × 32 = 1.536 MHz, WS = 50% at 24kHz.
     * ES8311 sees exactly the same clock as in non-TDM mode. */
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { false, false, false }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_std_cfg));

    /* RX: TDM Philips 4-slot for ES7210 4-channel ADC.
     * BCLK = 24000 × 4 × 16 = 1.536 MHz (identical to TX!).
     * WS = auto width = 4 × 16 / 2 = 32 BCLK high = 50% duty (identical to TX!).
     * Electrically compatible with ES8311's standard I2S expectation. */
    i2s_tdm_config_t rx_tdm_cfg = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG((uint32_t)input_sample_rate_),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                   I2S_TDM_SLOT2 | I2S_TDM_SLOT3)),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,  /* MCLK already claimed by TX */
            .bclk = bclk,
            .ws = ws,
            .dout = I2S_GPIO_UNUSED,
            .din = din,
            .invert_flags = { false, false, false }
        },
    };
    rx_tdm_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    rx_tdm_cfg.slot_cfg.total_slot = 4;
    rx_tdm_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    rx_tdm_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;

    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &rx_tdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    ESP_LOGI(TAG, "TDM duplex channels created (TX STD + RX TDM 4-ch)");
}

void BoxAudioCodec::SetOutputVolume(int volume) {
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

void BoxAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        // TDM 4-channel: 4 mics (MIC1=right, MIC2=left, MIC3=front, MIC4=back)
        // Standard 2-channel: only MIC1+MIC2
        uint8_t in_ch = (tdm_channels_ >= 4) ? 4 : 2;
        uint16_t in_mask = 0;
        for (int i = 0; i < in_ch; i++) {
            in_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(i);
        }
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = in_ch,
            .channel_mask = in_mask,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));
        /* 所有启用通道的 PGA 增益 */
        esp_codec_dev_set_in_channel_gain(input_dev_, in_mask, input_gain_);
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }
    AudioCodec::EnableInput(enable);
}

void BoxAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        // Play 16bit 1 channel
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &fs));
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    }
    AudioCodec::EnableOutput(enable);
}

int BoxAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_) return samples;

    /* codec_dev 开启了双通道 (channel_mask=0x3)，esp_codec_dev_read 返回交错立体声数据。
     * 语音处理框架期望单声道数据，所以需要：
     *   1. 读立体声到 temp buffer
     *   2. 转发立体声给声源定位器 (tdm_callback_)
     *   3. 提取 ch0 (MIC1) 给语音框架 */
    const int ch_per_frame = (tdm_channels_ >= 4) ? 4 : 2;
    static int16_t s_temp[2048];  // 静态缓冲区，足够 30ms@24kHz × 4ch
    int total_int16 = samples * ch_per_frame;
    if (total_int16 > (int)(sizeof(s_temp)/sizeof(s_temp[0]))) {
        total_int16 = sizeof(s_temp)/sizeof(s_temp[0]);
        samples = total_int16 / ch_per_frame;
    }

    int ret = esp_codec_dev_read(input_dev_, (void*)s_temp, total_int16 * sizeof(int16_t));
    if (ret != 0) {
        return 0;
    }

    /* 转发多通道原始数据给声源定位器 */
    if (tdm_callback_) {
        int frames = total_int16 / ch_per_frame;
        if (frames > 0) {
            tdm_callback_(s_temp, frames);
        }
    }

    /* 提取 ch0 (MIC1) 给语音处理框架 */
    for (int i = 0; i < samples; i++) {
        dest[i] = s_temp[i * ch_per_frame];
    }

    return samples;
}

int BoxAudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_) {
        static uint32_t s_write_count = 0;
        static int64_t s_last_log = 0;
        s_write_count++;
        int64_t now = esp_timer_get_time();
        if (now - s_last_log >= 2000000) {
            ESP_LOGI(TAG, "Write #%lu samples=%d vol=%d", (unsigned long)s_write_count, samples, output_volume_);
            s_last_log = now;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)));
    }
    return samples;
}

void BoxAudioCodec::PlayTestTone(int duration_ms, int freq_hz) {
    ESP_LOGI(TAG, "PlayTestTone: %d Hz for %d ms", freq_hz, duration_ms);
    if (!output_enabled_) {
        EnableOutput(true);
    }
    SetOutputVolume(80);

    const int sr = output_sample_rate_;
    const int total_samples = sr * duration_ms / 1000;
    const int frame_samples = 240;  // 10ms at 24kHz
    const float phase_inc = 2.0f * 3.14159265f * freq_hz / sr;
    float phase = 0.0f;

    auto* stereo = new int16_t[frame_samples * 2];
    for (int offset = 0; offset < total_samples; offset += frame_samples) {
        int n = (total_samples - offset > frame_samples) ? frame_samples : (total_samples - offset);
        for (int i = 0; i < n; i++) {
            int16_t val = (int16_t)(0.7f * 32767.0f * sinf(phase));
            phase += phase_inc;
            if (phase > 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
            stereo[i * 2]     = val;  // L
            stereo[i * 2 + 1] = val;  // R
        }
        esp_codec_dev_write(output_dev_, stereo, n * 2 * sizeof(int16_t));
    }
    delete[] stereo;
    ESP_LOGI(TAG, "PlayTestTone: done");
}