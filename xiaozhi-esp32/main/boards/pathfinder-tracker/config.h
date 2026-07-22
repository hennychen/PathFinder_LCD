#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/*
 * PathFinder Tracker Board
 * -----------------------
 * ESP32-S3-N16R8 (16MB Flash + 8MB Octal PSRAM)
 * Hardware stack:
 *   - AcousticEye V1.0: ES7210 (4-ch ADC) + ES8311 (DAC) + NS4150B (Class-D amp)
 *   - OV2640 camera (DVP)
 *   - 2x MG90S pan/tilt servos
 *   - PA_EN power enable GPIO (must be HIGH before any audio peripheral init)
 *
 * Audio architecture (BoxAudioCodec / Duplex):
 *   - ES7210 TDM 4-ch ADC → I2S0 RX (DIN=GPIO21)
 *   - ES8311 DAC → I2S0 TX (DOUT=GPIO3) → NS4150B amp → speaker pads
 *   - Shared I2S clocks: MCLK=GPIO42, BCLK=GPIO41, WS=GPIO40
 *   - Both codecs at 24kHz (BoxAudioCodec requires equal rates)
 */

/* ===================== Audio I/O (Duplex / BoxAudioCodec) ===================== */
/* AcousticEye V1.0: ES7210 + ES8311 share the same I2S bus.
 * ES7210 TDM 4-ch ADC at 24kHz (xiaozhi AudioService resamples 24k→16k for WakeNet).
 * ES8311 DAC at 24kHz → NS4150B Class-D amp → speaker pads on AcousticEye board.
 * BoxAudioCodec requires input_sample_rate == output_sample_rate. */
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

/* Shared I2S clocks for ES7210 (RX) and ES8311 (TX) */
#define AUDIO_I2S_GPIO_MCLK  GPIO_NUM_42   /* ES7210 MCLK / ES8311 MCLK */
#define AUDIO_I2S_GPIO_BCLK  GPIO_NUM_41   /* ES7210 BCLK / ES8311 BCLK */
#define AUDIO_I2S_GPIO_WS    GPIO_NUM_40   /* ES7210 WS   / ES8311 WS   */

/* Data lines */
#define AUDIO_I2S_GPIO_DIN   GPIO_NUM_21   /* ES7210 DOUT → ESP32 DIN (TDM 4-ch) */
#define AUDIO_I2S_GPIO_DOUT  GPIO_NUM_3    /* ESP32 DOUT → ES8311 DIN (standard I2S) */

/* ES8311 DAC output amplifier enable (NS4150B power control) */
#define AUDIO_CODEC_PA_PIN   GPIO_NUM_45

/* ES8311 I2C configuration */
#define AUDIO_CODEC_ES8311_ADDR  0x30   /* Try xiaozhi default; vendor doc says 0x18 but actual may differ */

/* ===================== ES7210 控制（TDM 4-ch 输入） ===================== */
#define ES7210_I2C_SDA   GPIO_NUM_38
#define ES7210_I2C_SCL   GPIO_NUM_39
#define ES7210_I2C_ADDR  0x80   /* 8-bit format (espressif codec dev shifts >>1 to get 7-bit 0x40) */
#define ES7210_I2C_PORT  0            /* I2C_NUM_0：摄像头 SCCB 占用了 port 1，ES7210 用 port 0 */
#define ES7210_TDM_CHANNELS 4         /* 4通道 TDM：CH0 喂 xiaozhi，CH0-3 留声源定位 */

/* ===================== 板级电源与LED ===================== */
/* PA_EN 必须 app_main 首行拉高，使AcousticEye音频电路（ES7210+ES8311+NS4150B）得电 */
#define PA_EN_GPIO              GPIO_NUM_45

/* 板载单点状态LED（兼做WS2812灯环数据线） */
#define BUILTIN_LED_GPIO        GPIO_NUM_48

/* WS2812 环形方向指示灯（36灯环）数据线 — 与 BUILTIN_LED_GPIO 共用 GPIO48
 * 注意：不能同时创建两个 RMT 设备在同一 GPIO 上，
 * GetLed() 必须返回 nullptr 以避免冲突。 */
#define WS2812_RING_GPIO        GPIO_NUM_48
#define WS2812_RING_COUNT       36

/* BOOT按键（GPIO0） - 短按切换对话状态，长按进入WiFi配网 */
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

/* ===================== 舵机云台（Phase 3 MCP工具使用） ===================== */
#define SERVO_PAN_GPIO          GPIO_NUM_47
#define SERVO_TILT_GPIO         GPIO_NUM_14

/* ===================== OV2640 摄像头 ===================== */
/* 与 bread-compact-wifi-s3cam 完全一致，已验证引脚匹配 PathFinder_Tracker */
#define CAMERA_PIN_D0    GPIO_NUM_11
#define CAMERA_PIN_D1    GPIO_NUM_9
#define CAMERA_PIN_D2    GPIO_NUM_8
#define CAMERA_PIN_D3    GPIO_NUM_10
#define CAMERA_PIN_D4    GPIO_NUM_12
#define CAMERA_PIN_D5    GPIO_NUM_18
#define CAMERA_PIN_D6    GPIO_NUM_17
#define CAMERA_PIN_D7    GPIO_NUM_16
#define CAMERA_PIN_PWDN  -1   /* 无电源控制 */
#define CAMERA_PIN_RESET -1   /* 硬件复位NC */
#define CAMERA_PIN_XCLK  GPIO_NUM_15
#define CAMERA_PIN_PCLK  GPIO_NUM_13
#define CAMERA_PIN_VSYNC GPIO_NUM_6
#define CAMERA_PIN_HREF  GPIO_NUM_7
#define CAMERA_PIN_SIOD  GPIO_NUM_4   /* SCCB SDA (I2C0) */
#define CAMERA_PIN_SIOC  GPIO_NUM_5   /* SCCB SCL (I2C0) */
#define XCLK_FREQ_HZ     16000000     /* 16MHz */

/* ===================== 显示屏（无） =====================
 * B板无LCD，board 返回 NoDisplay。
 * 这里不定义 DISPLAY_* 宏以避免触发 DISPLAY_LCD_TYPE choice 依赖。
 */

#endif // _BOARD_CONFIG_H_
