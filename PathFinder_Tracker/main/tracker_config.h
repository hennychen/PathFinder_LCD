#ifndef TRACKER_CONFIG_H
#define TRACKER_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"

/* ======================== AcousticEye ES7210 ======================== */
#define ES7210_I2C_SDA_GPIO         GPIO_NUM_38
#define ES7210_I2C_SCL_GPIO         GPIO_NUM_39
#define ES7210_I2C_PORT             I2C_NUM_1
#define ES7210_I2C_FREQ_HZ          400000
#define ES7210_I2C_ADDR             0x40

#define ES7210_I2S_NUM              I2S_NUM_0
#define ES7210_I2S_MCLK_GPIO        GPIO_NUM_42
#define ES7210_I2S_BCLK_GPIO        GPIO_NUM_41
#define ES7210_I2S_WS_GPIO          GPIO_NUM_40
#define ES7210_I2S_DIN_GPIO         GPIO_NUM_21
#define ES7210_SAMPLE_RATE          48000
#define ES7210_I2S_BITS             32

/* ======================== PA_EN 板级音频使能 ======================== */
#define PA_EN_GPIO                  GPIO_NUM_45  /* 原板 GPIO13,因 OV2640 PCLK 占用迁移 */

/* ======================== WS2812 灯环 ======================== */
#define WS2812_GPIO                 GPIO_NUM_48
#define WS2812_LED_COUNT            36

/* ======================== MG90S 舵机 ======================== */
#define SERVO_PAN_GPIO              GPIO_NUM_14
#define SERVO_TILT_GPIO             GPIO_NUM_47
#define SERVO_FREQ_HZ               50
#define SERVO_MIN_PULSE_US          500
#define SERVO_MAX_PULSE_US          2500
#define SERVO_MIN_ANGLE             0
#define SERVO_MAX_ANGLE             180

/* ======================== UART 板间通信 ======================== */
/* NOTE: GPIO33-37 被 Octal PSRAM 占用, 不可用。
   暂用 GPIO19/20 (USB-JTAG引脚, 已切到UART0控制台后释放)。 */
#define UART_PORT_NUM               UART_NUM_1
#define UART_TX_GPIO                GPIO_NUM_19
#define UART_RX_GPIO                GPIO_NUM_20
#define UART_BAUD_RATE              115200
#define UART_BUF_SIZE               256

/* ======================== xiaozhi AI TTS (Phase 4 预留) ======================== */
#define TTS_I2S_NUM                 I2S_NUM_1
#define TTS_BCLK_GPIO               GPIO_NUM_1
#define TTS_WS_GPIO                 GPIO_NUM_2
#define TTS_DOUT_GPIO               GPIO_NUM_3

/* ======================== OV2640 摄像头 (DVP) ======================== */
#define CAM_PIN_PWDN    -1   /* 无电源控制 */
#define CAM_PIN_RESET   -1   /* 硬件复位, NC */
#define CAM_PIN_D0      GPIO_NUM_11
#define CAM_PIN_D1      GPIO_NUM_9
#define CAM_PIN_D2      GPIO_NUM_8
#define CAM_PIN_D3      GPIO_NUM_10
#define CAM_PIN_D4      GPIO_NUM_12
#define CAM_PIN_D5      GPIO_NUM_18
#define CAM_PIN_D6      GPIO_NUM_17
#define CAM_PIN_D7      GPIO_NUM_16
#define CAM_PIN_VSYNC   GPIO_NUM_6
#define CAM_PIN_HREF    GPIO_NUM_7
#define CAM_PIN_PCLK    GPIO_NUM_13
#define CAM_PIN_XCLK    GPIO_NUM_15
#define CAM_PIN_SIOD    GPIO_NUM_4   /* SCCB SDA (I2C0) */
#define CAM_PIN_SIOC    GPIO_NUM_5   /* SCCB SCL (I2C0) */
#define CAM_XCLK_FREQ   16000000     /* 16MHz */

/* ======================== Mesh 组网 ======================== */
/* A板(EMOTE)=ROOT, B板(Tracker)=CHILD */
#define MESH_ROLE_NODE           0   /* 0=CHILD, 1=ROOT (defined here for reference) */

/* ======================== 状态机枚举 ======================== */
typedef enum {
    TRACKER_STATE_IDLE = 0,
    TRACKER_STATE_ACOUSTIC_TRACK,
    TRACKER_STATE_FACE_TRACK,
    TRACKER_STATE_SEARCH,
} tracker_state_t;

#endif /* TRACKER_CONFIG_H */
