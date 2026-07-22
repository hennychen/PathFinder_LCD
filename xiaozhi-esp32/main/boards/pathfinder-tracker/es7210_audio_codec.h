/*
 * es7210_audio_codec.h
 *
 * Custom AudioCodec for PathFinder Tracker:
 *   - Speaker output: I2S0 STD mode → MAX98357A (inherited from NoAudioCodec)
 *   - Microphone input: I2S1 TDM mode → ES7210 4-channel ADC
 *
 * The ES7210 outputs 4-channel TDM audio. This codec reads the TDM stream
 * and extracts CH0 (slot 0) to feed xiaozhi's voice pipeline as int16_t.
 * The remaining channels are available for future sound-source localization.
 */

#ifndef _ES7210_AUDIO_CODEC_H
#define _ES7210_AUDIO_CODEC_H

#include "codecs/no_audio_codec.h"
#include "es7210.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>

class Es7210AudioCodec : public NoAudioCodec {
public:
    /**
     * @param input_sample_rate  ES7210 sample rate (e.g. 16000)
     * @param output_sample_rate MAX98357A sample rate (e.g. 24000)
     * @param spk_bclk  Speaker I2S BCLK  (MAX98357A)
     * @param spk_ws    Speaker I2S WS/LRCK
     * @param spk_dout  Speaker I2S DOUT
     * @param mic_sck   ES7210 BCLK  (TDM)
     * @param mic_ws    ES7210 WS    (TDM)
     * @param mic_din   ES7210 DOUT  (TDM data in to ESP32)
     * @param mic_mclk  ES7210 MCLK
     * @param i2c_sda   ES7210 I2C SDA
     * @param i2c_scl   ES7210 I2C SCL
     * @param i2c_addr  ES7210 I2C 7-bit address (typically 0x40)
     */
    Es7210AudioCodec(int input_sample_rate, int output_sample_rate,
                     gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
                     gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din, gpio_num_t mic_mclk,
                     gpio_num_t i2c_sda, gpio_num_t i2c_scl, uint8_t i2c_addr);

    ~Es7210AudioCodec() override;

    /**
     * Read CH0 audio from ES7210 TDM stream.
     * Internally reads 4-channel interleaved int16_t TDM data,
     * then extracts every 4th sample (slot 0) into dest.
     */
    int Read(int16_t* dest, int samples) override;

    /* Speaker diagnostics: trace EnableOutput / Write path */
    void EnableOutput(bool enable) override;
    int Write(const int16_t* data, int samples) override;

private:
    void InitSpeakerChannel(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout);
    void InitEs7210Codec(gpio_num_t sda, gpio_num_t scl, uint8_t addr);
    void InitTdmRxChannel(gpio_num_t sck, gpio_num_t ws, gpio_num_t din, gpio_num_t mclk);

    i2c_master_bus_handle_t i2c_bus_    = nullptr;
    es7210_dev_handle_t     es7210_dev_ = nullptr;

    /* DC blocker state: y[n] = x[n] - x[n-1] + R * y[n-1] */
    float dc_prev_in_  = 0.0f;
    float dc_prev_out_ = 0.0f;
    static constexpr float DC_R = 0.995f;
};

#endif // _ES7210_AUDIO_CODEC_H
