#ifndef _BOX_AUDIO_CODEC_H
#define _BOX_AUDIO_CODEC_H

#include "audio_codec.h"

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mutex>


/* Callback for receiving raw 4-channel TDM data (for sound localization).
 * Called from Read() after each i2s_channel_read. Signature: (interleaved_data, frame_count) */
typedef void (*TdmDataCallback)(const int16_t* tdm_data, int frames);

class BoxAudioCodec : public AudioCodec {
private:
    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* out_ctrl_if_ = nullptr;
    const audio_codec_if_t* out_codec_if_ = nullptr;
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;
    const audio_codec_if_t* in_codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;

    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;
    std::mutex data_if_mutex_;

    int tdm_channels_ = 0;  /* 0 = standard I2S (default), 4 = TDM 4-ch for ES7210 */
    TdmDataCallback tdm_callback_ = nullptr;  /* optional: receive raw 4-ch data */

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);
    void CreateTdmDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    BoxAudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
        gpio_num_t pa_pin, uint8_t es8311_addr, uint8_t es7210_addr, bool input_reference,
        int tdm_channels = 0);
    virtual ~BoxAudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;

    /* Set callback to receive raw 4-channel TDM data for sound-source localization */
    void SetTdmDataCallback(TdmDataCallback cb) { tdm_callback_ = cb; }

public:
    /* Diagnostic: write sine wave to ES8311 DAC to verify audio output path */
    void PlayTestTone(int duration_ms = 2000, int freq_hz = 1000);
};

#endif // _BOX_AUDIO_CODEC_H
