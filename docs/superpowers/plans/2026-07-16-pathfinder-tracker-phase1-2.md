# PathFinder Tracker Phase 1-2：声源定位→舵机闭环 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在新 ESP32-S3 N16R8 上搭建 PathFinder_Tracker 项目，实现 AcousticEye 声源定位 → MG90S 舵机转向的完整闭环。

**Architecture:** 迁移 AcousticEye 现有 ES7210 驱动和 GCC-PHAT 声源算法到新项目，新增 LEDC PWM 舵机驱动和 UART 通信模块。Core0 运行音频采集+声源计算+舵机控制单任务，通过角度映射驱动 Pan/Tilt 双轴。板子B 通过 UART 向板子A 上报追踪状态。

**Tech Stack:** ESP-IDF v6.0, C, FreeRTOS, I2C（ES7210配置）, I2S（ES7210音频流）, LEDC（PWM舵机）, RMT（WS2812）, UART（板间通信）

**Spec:** `docs/superpowers/specs/2026-07-16-pathfinder-tracker-multimodal-design.md`

---

## File Structure

```
PathFinder_Tracker/
├── CMakeLists.txt                      # 项目根 CMake
├── partitions.csv                      # 16MB Flash 分区表
├── sdkconfig.defaults                  # 默认配置
├── main/
│   ├── CMakeLists.txt                  # main 组件注册
│   ├── idf_component.yml               # 组件依赖
│   ├── main.c                          # 入口：初始化 + 创建任务
│   ├── tracker_config.h                # 全局引脚定义与常量
│   ├── drivers/
│   │   ├── drv_es7210.h               # ES7210 I2C配置 + I2S采集接口
│   │   ├── drv_es7210.c               # 从AcousticEye迁移
│   │   ├── drv_servo.h                # LEDC PWM双轴舵机接口
│   │   ├── drv_servo.c                # 新实现
│   │   ├── drv_ws2812.h               # WS2812灯环接口
│   │   ├── drv_ws2812.c               # 从AcousticEye迁移
│   │   ├── drv_uart_comm.h            # UART协议接口
│   │   └── drv_uart_comm.c            # 新实现
│   ├── audio/
│   │   ├── sound_localizer.h          # GCC-PHAT声源定位接口
│   │   └── sound_localizer.c          # 从AcousticEye calc_direction.c迁移
│   └── comm/
│       └── tracker_protocol.h         # UART帧定义
└── components/
    └── es7210/                         # ES7210寄存器驱动（从AcousticEye复制）
        ├── CMakeLists.txt
        ├── include/es7210.h
        ├── priv_include/es7210_reg.h
        └── es7210.c
```

---

## Task 1: 项目脚手架

**Files:**
- Create: `PathFinder_Tracker/CMakeLists.txt`
- Create: `PathFinder_Tracker/sdkconfig.defaults`
- Create: `PathFinder_Tracker/partitions.csv`
- Create: `PathFinder_Tracker/main/CMakeLists.txt`
- Create: `PathFinder_Tracker/main/idf_component.yml`
- Create: `PathFinder_Tracker/main/tracker_config.h`
- Create: `PathFinder_Tracker/main/main.c`

- [ ] **Step 1: 创建项目根 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(PathFinder_Tracker)
```

- [ ] **Step 2: 创建 sdkconfig.defaults**

```ini
# ESP32-S3 N16R8 配置
# PSRAM: Octal 模式 80MHz
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y

# CPU 240MHz 双核
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# Flash 16MB
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"

# FreeRTOS
CONFIG_FREERTOS_HZ=1000

# Wi-Fi（为Phase 4 xiaozhi AI预留）
CONFIG_ESP_WIFI_ENABLED=y

# I2S
CONFIG_SOC_I2S_SUPPORTED=y

# 编译优化
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
```

- [ ] **Step 3: 创建分区表**

```csv
# Name,   Type, SubType, Offset,   Size,  Flags
# PathFinder_Tracker 分区表 (16MB Flash)
nvs,       data, nvs,     0x9000,   0x4000,
phy_init,  data, phy,     0xf000,   0x1000,
factory,   app,  factory, 0x10000,  4M,
model,     data, spiffs,  ,         4M,
```

- [ ] **Step 4: 创建 tracker_config.h（全局引脚定义）**

```c
#ifndef TRACKER_CONFIG_H
#define TRACKER_CONFIG_H

#include "driver/gpio.h"

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
#define UART_PORT_NUM               UART_NUM_1
#define UART_TX_GPIO                GPIO_NUM_43
#define UART_RX_GPIO                GPIO_NUM_44
#define UART_BAUD_RATE              115200
#define UART_BUF_SIZE               256

/* ======================== xiaozhi AI TTS (Phase 4 预留) ======================== */
#define TTS_I2S_NUM                 I2S_NUM_1
#define TTS_BCLK_GPIO               GPIO_NUM_1
#define TTS_WS_GPIO                 GPIO_NUM_2
#define TTS_DOUT_GPIO               GPIO_NUM_3

/* ======================== 状态机枚举 ======================== */
typedef enum {
    TRACKER_STATE_IDLE = 0,
    TRACKER_STATE_ACOUSTIC_TRACK,
    TRACKER_STATE_FACE_TRACK,
    TRACKER_STATE_SEARCH,
} tracker_state_t;

#endif /* TRACKER_CONFIG_H */
```

- [ ] **Step 5: 创建最小 main.c（仅启动日志）**

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tracker_config.h"

static const char *TAG = "tracker_main";

void app_main(void)
{
    printf("[%s] PathFinder_Tracker booting on ESP32-S3 N16R8\n", TAG);
    printf("[%s] Phase 1: Hardware driver verification\n", TAG);

    while (1) {
        printf("[%s] heartbeat\n", TAG);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

- [ ] **Step 6: 创建 main/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
        "drivers"
        "audio"
        "comm"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_uart
        esp_driver_ledc
        esp_timer
        nvs_flash
)
```

- [ ] **Step 7: 创建 idf_component.yml**

```yaml
dependencies:
  idf:
    version: '>=5.0'
```

- [ ] **Step 8: 编译验证**

Run: `cd PathFinder_Tracker && idf.py build`
Expected: 编译成功，无错误

- [ ] **Step 9: 烧录验证**

Run: `cd PathFinder_Tracker && idf.py -p /dev/cu.usbmodem* flash monitor`
Expected: 串口输出 `[tracker_main] PathFinder_Tracker booting on ESP32-S3 N16R8` 和每2秒一次的 heartbeat

- [ ] **Step 10: 提交**

```bash
cd PathFinder_Tracker && git add -A
git commit -m "feat: scaffold PathFinder_Tracker project on N16R8"
```

---

## Task 2: ES7210 驱动迁移

将 AcousticEye 项目的 ES7210 组件和声源定位算法迁移到 PathFinder_Tracker，引脚改为 spec 定义的新 GPIO。

**Files:**
- Copy: `AcousticEye/.../components/es7210/` → `PathFinder_Tracker/components/es7210/`
- Create: `PathFinder_Tracker/main/drivers/drv_es7210.h`
- Create: `PathFinder_Tracker/main/drivers/drv_es7210.c`
- Create: `PathFinder_Tracker/main/audio/sound_localizer.h`
- Create: `PathFinder_Tracker/main/audio/sound_localizer.c`
- Modify: `PathFinder_Tracker/main/CMakeLists.txt`
- Modify: `PathFinder_Tracker/main/main.c`

- [ ] **Step 1: 复制 ES7210 组件**

Run:
```bash
cd /Users/pm/PathFinder_LCD
cp -r AcousticEye/AcousticEye/components/es7210 PathFinder_Tracker/components/
```

- [ ] **Step 2: 创建 components/es7210/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "es7210.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_driver_i2c
)
```

- [ ] **Step 3: 创建 drv_es7210.h — 封装初始化和数据读取接口**

```c
#ifndef DRV_ES7210_H
#define DRV_ES7210_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define ES7210_CHANNELS 4
#define ES7210_SAMPLE_SIZE 256

/**
 * 初始化 ES7210：I2C 配置 + I2S0 四通道音频采集
 * 返回 ESP_OK 或错误码
 */
esp_err_t drv_es7210_init(void);

/**
 * 读取一帧四通道音频数据
 * data_out: [4][ES7210_SAMPLE_SIZE] 的 float 数组
 */
esp_err_t drv_es7210_read(float data_out[ES7210_CHANNELS][ES7210_SAMPLE_SIZE]);

/**
 * 获取 I2S0 句柄（供 xiaozhi AI Phase 4 使用）
 */
int drv_es7210_get_i2s_port(void);

#endif /* DRV_ES7210_H */
```

- [ ] **Step 4: 创建 drv_es7210.c — 封装 ES7210 + I2S 初始化**

```c
#include "drv_es7210.h"
#include "tracker_config.h"
#include "es7210.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "drv_es7210";

static i2s_chan_handle_t s_rx_handle = NULL;
static es7210_dev_handle_t s_es7210_handle = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;

/* TDM 原始读取缓冲区：4通道 × 32bit/sample */
#define RAW_BUF_BYTES  (ES7210_SAMPLE_SIZE * ES7210_CHANNELS * 4)
static int32_t s_raw_buf[ES7210_SAMPLE_SIZE * ES7210_CHANNELS];

esp_err_t drv_es7210_init(void)
{
    ESP_LOGI(TAG, "Initializing ES7210 on new GPIO pins");

    /* 1. I2C 主总线初始化 */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = ES7210_I2C_PORT,
        .sda_io_num = ES7210_I2C_SDA_GPIO,
        .scl_io_num = ES7210_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    /* 2. ES7210 编解码器配置 */
    es7210_i2c_config_t i2c_conf = {
        .i2c_bus_handle = s_i2c_bus,
        .i2c_addr = ES7210_I2C_ADDR,
    };
    ESP_ERROR_CHECK(es7210_new_codec(&i2c_conf, &s_es7210_handle));

    es7210_codec_config_t codec_cfg = {
        .sample_rate_hz = ES7210_SAMPLE_RATE,
        .mclk_ratio = 256,
        .i2s_format = ES7210_I2S_FMT_DSP_A,
        .bit_width = ES7210_I2S_BITS_24B,
        .mic_bias = ES7210_MIC_BIAS_2V87,
        .mic_gain = ES7210_MIC_GAIN_30DB,
        .flags.tdm_enable = 1,
    };
    ESP_ERROR_CHECK(es7210_config_codec(s_es7210_handle, &codec_cfg));

    /* 3. I2S0 RX 通道初始化 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ES7210_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.dma_buf_align = 64;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_handle));

    /* TDM 四通道格式：32bit slot, 48kHz, 4 slots */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ES7210_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .bclk = ES7210_I2S_BCLK_GPIO,
            .ws   = ES7210_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = ES7210_I2S_DIN_GPIO,
            .mclk = ES7210_I2S_MCLK_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    /* 覆盖 MCLK multiple 为 256 */
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));

    ESP_LOGI(TAG, "ES7210 ready: I2C@%d SDA=%d SCL=%d, I2S0 BCLK=%d WS=%d DIN=%d MCLK=%d",
             ES7210_I2C_PORT, ES7210_I2C_SDA_GPIO, ES7210_I2C_SCL_GPIO,
             ES7210_I2S_BCLK_GPIO, ES7210_I2S_WS_GPIO, ES7210_I2S_DIN_GPIO, ES7210_I2S_MCLK_GPIO);

    return ESP_OK;
}

esp_err_t drv_es7210_read(float data_out[ES7210_CHANNELS][ES7210_SAMPLE_SIZE])
{
    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(s_rx_handle, s_raw_buf, RAW_BUF_BYTES, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK || bytes_read != RAW_BUF_BYTES) {
        ESP_LOGW(TAG, "I2S read error: %s, got %d/%d bytes",
                 esp_err_to_name(ret), (int)bytes_read, RAW_BUF_BYTES);
        return ESP_FAIL;
    }

    /* 解交织：TDM 格式为 CH0_CH1_CH2_CH3 循环，每个 slot 32bit */
    for (int i = 0; i < ES7210_SAMPLE_SIZE; i++) {
        for (int ch = 0; ch < ES7210_CHANNELS; ch++) {
            int32_t raw = s_raw_buf[i * ES7210_CHANNELS + ch];
            /* 右移 8 位将 24bit 有效数据归一化，再缩放到 [-1, 1) */
            data_out[ch][i] = (float)(raw >> 8) / 8388608.0f;
        }
    }

    return ESP_OK;
}

int drv_es7210_get_i2s_port(void)
{
    return ES7210_I2S_NUM;
}
```

- [ ] **Step 5: 创建 sound_localizer.h — 声源定位接口**

```c
#ifndef SOUND_LOCALIZER_H
#define SOUND_LOCALIZER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * 声源定位结果
 */
typedef struct {
    float angle;        /* 0~360 度，-1 表示无效 */
    float confidence;   /* 0~1 置信度 */
    bool  valid;        /* 是否有效 */
} localization_result_t;

/**
 * 初始化声源定位算法（FFT 窗函数等）
 */
void sound_localizer_init(float sample_rate);

/**
 * 设置声音活动量阈值
 */
void sound_localizer_set_threshold(float threshold);

/**
 * 计算一帧音频的声源方向
 * 返回 localization_result_t
 */
localization_result_t sound_localizer_compute(float mic_data[4][256]);

#endif /* SOUND_LOCALIZER_H */
```

- [ ] **Step 6: 创建 sound_localizer.c — 从 AcousticEye calc_direction.c 迁移 GCC-PHAT 算法**

此文件从 `AcousticEye/AcousticEye/components/calc/calc_direction.c` 迁移核心算法。保留 GCC-PHAT 互相关 + FFT + 角度计算逻辑，移除 I2S 初始化代码（已由 drv_es7210.c 负责），改为接收外部传入的音频数据。

```c
#include "sound_localizer.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"

static const char *TAG = "sound_loc";

#define N_SAMPLES 256
#define N_FFT     256

static float s_wind_hann[N_SAMPLES];
static float s_fft_input[N_FFT * 2];   /* 实部+虚部交替 */
static float s_activity_threshold = -2.7f;
static int   s_max_lag = 5;  /* 最大搜索延迟（采样点），由采样率和麦克风间距决定 */
static uint32_t s_sample_rate = 48000;

/* 麦克风对间距（米），声速 343 m/s */
#define MIC_SPACING 0.04
#define SOUND_SPEED 343.0f

void sound_localizer_init(float sample_rate)
{
    s_sample_rate = (uint32_t)sample_rate;
    /* 初始化 Hann 窗 */
    dsps_wind_hann_f32(s_wind_hann, N_SAMPLES);
    /* 初始化 FFT 表 */
    dsps_fft2r_init_sc16(NULL, N_FFT);
    /* 预计算 max_lag = mic_spacing * sample_rate / sound_speed */
    s_max_lag = (int)(0.04f * sample_rate / 343.0f);
    if (s_max_lag < 1) s_max_lag = 1;
    ESP_LOGI(TAG, "Sound localizer initialized, sample_rate=%.0f, max_lag=%d", sample_rate, s_max_lag);
}

void sound_localizer_set_threshold(float threshold)
{
    s_activity_threshold = threshold;
}

/* 计算两个通道之间的 GCC-PHAT 互相关峰值延迟 */
static float gcc_phat_delay(const float *a, const float *b, int n)
{
    /* A 的 FFT */
    for (int i = 0; i < n; i++) {
        s_fft_input[i * 2]     = a[i] * s_wind_hann[i];
        s_fft_input[i * 2 + 1] = 0;
    }
    dsps_fft2r_sc16(s_fft_input, n);

    /* B 的 FFT */
    float fft_b[n * 2];
    for (int i = 0; i < n; i++) {
        fft_b[i * 2]     = b[i] * s_wind_hann[i];
        fft_b[i * 2 + 1] = 0;
    }
    dsps_fft2r_sc16(fft_b, n);

    /* 互相关 = A * conj(B)，加权 PHAT */
    float cross[n * 2];
    for (int i = 0; i < n; i++) {
        float ar = s_fft_input[i * 2], ai = s_fft_input[i * 2 + 1];
        float br = fft_b[i * 2], bi = fft_b[i * 2 + 1];
        /* conj(B) = br - j*bi */
        float rr = ar * br + ai * bi;
        float ri = ai * br - ar * bi;
        float mag = sqrtf(rr * rr + ri * ri) + 1e-10f;
        /* PHAT 加权 */
        cross[i * 2]     = rr / mag;
        cross[i * 2 + 1] = ri / mag;
    }

    /* IFFT */
    dsps_bit_rev_sc16(cross, n);
    dsps_cplx2real_sc16(cross, n);

    /* 在合理延迟范围内搜索峰值 */
    int max_lag = s_max_lag;  /* 使用预计算值 */
    float max_val = -1e30f;
    int best_lag = 0;
    for (int lag = -max_lag; lag <= max_lag; lag++) {
        int idx = (lag + n) % n;
        float val = cross[idx * 2];
        if (val > max_val) {
            max_val = val;
            best_lag = lag;
        }
    }

    return (float)best_lag;
}

localization_result_t sound_localizer_compute(float mic_data[4][256])
{
    localization_result_t result = { .angle = -1.0f, .confidence = 0.0f, .valid = false };

    /* 计算平均 RMS 作为活动量检测 */
    float rms_sum = 0;
    for (int ch = 0; ch < 4; ch++) {
        float sum_sq = 0;
        for (int i = 0; i < 256; i++) {
            sum_sq += mic_data[ch][i] * mic_data[ch][i];
        }
        rms_sum += sqrtf(sum_sq / 256.0f);
    }
    float avg_rms = rms_sum / 4.0f;
    float activity = log10f(avg_rms + 1e-10f);

    if (activity < s_activity_threshold) {
        return result; /* 声音太弱，跳过 */
    }

    /* 使用对角麦克风对计算 X/Y 延迟 */
    /* 通道映射假设: CH0=前, CH1=右, CH2=后, CH3=左 */
    float delay_x = gcc_phat_delay(mic_data[1], mic_data[3]); /* 右-左 */
    float delay_y = gcc_phat_delay(mic_data[0], mic_data[2]); /* 前-后 */

    /* 延迟→方向向量 */
    float dx = delay_x;
    float dy = delay_y;
    float mag = sqrtf(dx * dx + dy * dy);

    if (mag < 0.5f) {
        return result; /* 方向不明显 */
    }

    /* atan2 得到角度，0度=前，顺时针 */
    float angle_rad = atan2f(dx, dy);
    float angle_deg = angle_rad * 180.0f / 3.14159265f;
    if (angle_deg < 0) angle_deg += 360.0f;

    result.angle = angle_deg;
    result.confidence = (mag > 3.0f) ? 1.0f : (mag / 3.0f);
    result.valid = true;

    return result;
}
```

> **注意：** 上面的 `sample_rate_dummy()` 是占位说明。实际实现中，`max_lag` 在 `sound_localizer_init()` 中根据采样率预计算为静态变量 `s_max_lag`。AcousticEye 原始代码使用 `#define ES7210_SAMPLE_RATE 48000`，对应 `max_lag = (int)(0.04 * 48000 / 343) ≈ 5`。

- [ ] **Step 7: 更新 main/CMakeLists.txt 添加新源文件**

```cmake
idf_component_register(
    SRCS
        "main.c"
        "drivers/drv_es7210.c"
        "audio/sound_localizer.c"
    INCLUDE_DIRS
        "."
        "drivers"
        "audio"
        "comm"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_uart
        esp_driver_ledc
        esp_timer
        nvs_flash
        esp_dsp
)
```

- [ ] **Step 8: 更新 main.c 测试 ES7210 读取**

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tracker_config.h"
#include "drv_es7210.h"
#include "sound_localizer.h"

static const char *TAG = "tracker_main";

void app_main(void)
{
    printf("[%s] PathFinder_Tracker booting on ESP32-S3 N16R8\n", TAG);

    /* 初始化 ES7210 */
    esp_err_t ret = drv_es7210_init();
    if (ret != ESP_OK) {
        printf("[%s] ES7210 init FAILED: %s\n", TAG, esp_err_to_name(ret));
        return;
    }

    /* 初始化声源定位 */
    sound_localizer_init(ES7210_SAMPLE_RATE);
    sound_localizer_set_threshold(-2.7f);

    /* 主循环：读取音频 + 计算角度 */
    float mic_data[4][256];
    while (1) {
        if (drv_es7210_read(mic_data) == ESP_OK) {
            localization_result_t result = sound_localizer_compute(mic_data);
            if (result.valid) {
                printf("angle: %.1f deg (conf=%.2f)\n", result.angle, result.confidence);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

- [ ] **Step 9: 添加 esp_dsp 依赖到 idf_component.yml**

```yaml
dependencies:
  idf:
    version: '>=5.0'
  esp-dsp:
    version: '>=1.5'
```

- [ ] **Step 10: 编译验证**

Run: `cd PathFinder_Tracker && idf.py build`
Expected: 编译成功

- [ ] **Step 11: 硬件验证 — 烧录后拍手测试**

Run: `cd PathFinder_Tracker && idf.py -p /dev/cu.usbmodem* flash monitor`
Expected: 串口在拍手时输出 `angle: xxx.x deg (conf=x.xx)`，安静时无输出

- [ ] **Step 12: 提交**

```bash
git add -A
git commit -m "feat: migrate ES7210 driver and GCC-PHAT sound localizer"
```

---

## Task 3: WS2812 灯环驱动迁移

**Files:**
- Create: `PathFinder_Tracker/main/drivers/drv_ws2812.h`
- Create: `PathFinder_Tracker/main/drivers/drv_ws2812.c`
- Modify: `PathFinder_Tracker/main/CMakeLists.txt`
- Modify: `PathFinder_Tracker/main/main.c`

- [ ] **Step 1: 创建 drv_ws2812.h**

```c
#ifndef DRV_WS2812_H
#define DRV_WS2812_H

#include <stdint.h>
#include "esp_err.h"

/**
 * 初始化 WS2812 灯环（36颗 LED）
 */
esp_err_t drv_ws2812_init(void);

/**
 * 清除所有 LED（熄灭）
 */
void drv_ws2812_clear(void);

/**
 * 设置指定 LED 的 RGB 颜色
 */
void drv_ws2812_set_led(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * 将缓冲区刷新到物理 LED
 */
void drv_ws2812_show(void);

/**
 * 将 0~360 度角度映射到灯环并显示（红色指示）
 */
void drv_ws2812_show_angle(float angle);

#endif /* DRV_WS2812_H */
```

- [ ] **Step 2: 创建 drv_ws2812.c**

```c
#include "drv_ws2812.h"
#include "tracker_config.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "drv_ws2812";
static led_strip_handle_t s_strip = NULL;

esp_err_t drv_ws2812_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = WS2812_GPIO,
        .max_leds = WS2812_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10MHz */
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 ready: GPIO=%d, %d LEDs", WS2812_GPIO, WS2812_LED_COUNT);
    return ESP_OK;
}

void drv_ws2812_clear(void)
{
    if (s_strip) led_strip_clear(s_strip);
}

void drv_ws2812_set_led(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip && index < WS2812_LED_COUNT) {
        led_strip_set_pixel(s_strip, index, r, g, b);
    }
}

void drv_ws2812_show(void)
{
    if (s_strip) led_strip_refresh(s_strip);
}

void drv_ws2812_show_angle(float angle)
{
    if (!s_strip) return;
    if (angle < 0 || angle >= 360) {
        drv_ws2812_clear();
        drv_ws2812_show();
        return;
    }

    drv_ws2812_clear();
    uint32_t led_idx = (uint32_t)(angle / 10.0f) % WS2812_LED_COUNT;
    drv_ws2812_set_led(led_idx, 255, 0, 0);
    drv_ws2812_show();
}
```

- [ ] **Step 3: 更新 main/CMakeLists.txt 添加 ws2812 源文件和 led_strip 依赖**

在 SRCS 列表中添加 `"drivers/drv_ws2812.c"`，在 REQUIRES 中添加 `led_strip`

- [ ] **Step 4: 更新 main.c 集成 WS2812**

在 `app_main` 中 ES7210 初始化之前添加：
```c
drv_ws2812_init();
```

在声源角度输出处添加：
```c
if (result.valid) {
    printf("angle: %.1f deg (conf=%.2f)\n", result.angle, result.confidence);
    drv_ws2812_show_angle(result.angle);
}
```

- [ ] **Step 5: 烧录验证 — 拍手后灯环应指向声源方向**

Run: `idf.py -p /dev/cu.usbmodem* flash monitor`
Expected: 拍手时灯环上对应位置的 LED 亮红色

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add WS2812 LED ring driver with angle indicator"
```

---

## Task 4: MG90S 舵机驱动

**Files:**
- Create: `PathFinder_Tracker/main/drivers/drv_servo.h`
- Create: `PathFinder_Tracker/main/drivers/drv_servo.c`
- Modify: `PathFinder_Tracker/main/CMakeLists.txt`
- Modify: `PathFinder_Tracker/main/main.c`

- [ ] **Step 1: 创建 drv_servo.h**

```c
#ifndef DRV_SERVO_H
#define DRV_SERVO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    SERVO_PAN = 0,
    SERVO_TILT = 1,
} servo_id_t;

/**
 * 初始化双轴舵机（Pan + Tilt），LEDC PWM 50Hz
 */
esp_err_t drv_servo_init(void);

/**
 * 设置舵机角度（0~180度）
 */
esp_err_t drv_servo_set_angle(servo_id_t id, float angle);

/**
 * 获取当前角度
 */
float drv_servo_get_angle(servo_id_t id);

/**
 * 将声源角度（0~360度）映射为 Pan 舵机角度（0~180度）
 * 前方=90度，右=0度，左=180度
 */
float drv_servo_angle_from_sound(float sound_angle);

#endif /* DRV_SERVO_H */
```

- [ ] **Step 2: 创建 drv_servo.c**

```c
#include "drv_servo.h"
#include "tracker_config.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "drv_servo";

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES       LEDC_TIMER_14_BIT  /* 2^14 = 16384 */
#define LEDC_DUTY_MAX       16383
#define LEDC_FREQ_HZ        SERVO_FREQ_HZ

/* 舵机引脚映射 */
static const gpio_num_t s_servo_pins[2] = { SERVO_PAN_GPIO, SERVO_TILT_GPIO };
static const ledc_channel_t s_servo_channels[2] = { LEDC_CHANNEL_0, LEDC_CHANNEL_1 };
static float s_current_angles[2] = { 90.0f, 90.0f };

/* 角度→脉宽（微秒）：0°=500us, 180°=2500us */
static uint32_t angle_to_us(float angle)
{
    if (angle < SERVO_MIN_ANGLE) angle = SERVO_MIN_ANGLE;
    if (angle > SERVO_MAX_ANGLE) angle = SERVO_MAX_ANGLE;
    float us = SERVO_MIN_PULSE_US +
        (angle / 180.0f) * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US);
    return (uint32_t)us;
}

/* 脉宽→LEDC duty 值 */
static uint32_t us_to_duty(uint32_t us)
{
    /* 周期 = 1000000 / 50 = 20000 us = 20ms */
    /* duty = us / 20000 * 16384 */
    return (uint32_t)((uint64_t)us * LEDC_DUTY_MAX / 20000);
}

esp_err_t drv_servo_init(void)
{
    ESP_LOGI(TAG, "Initializing MG90S servos: Pan=GPIO%d Tilt=GPIO%d",
             SERVO_PAN_GPIO, SERVO_TILT_GPIO);

    /* 配置 LEDC 定时器 */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* 配置双通道 */
    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t ch_cfg = {
            .speed_mode = LEDC_MODE,
            .channel = s_servo_channels[i],
            .timer_sel = LEDC_TIMER,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = s_servo_pins[i],
            .duty = us_to_duty(angle_to_us(s_current_angles[i])),
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    }

    ESP_LOGI(TAG, "Servos ready: 50Hz, %d-bit duty", LEDC_DUTY_RES + 1);
    return ESP_OK;
}

esp_err_t drv_servo_set_angle(servo_id_t id, float angle)
{
    if (id < 0 || id > 1) return ESP_ERR_INVALID_ARG;
    if (angle < SERVO_MIN_ANGLE) angle = SERVO_MIN_ANGLE;
    if (angle > SERVO_MAX_ANGLE) angle = SERVO_MAX_ANGLE;

    uint32_t us = angle_to_us(angle);
    uint32_t duty = us_to_duty(us);

    esp_err_t ret = ledc_set_duty(LEDC_MODE, s_servo_channels[id], duty);
    if (ret != ESP_OK) return ret;
    ret = ledc_update_duty(LEDC_MODE, s_servo_channels[id]);
    if (ret != ESP_OK) return ret;

    s_current_angles[id] = angle;
    return ESP_OK;
}

float drv_servo_get_angle(servo_id_t id)
{
    if (id < 0 || id > 1) return -1;
    return s_current_angles[id];
}

float drv_servo_angle_from_sound(float sound_angle)
{
    /* 声源角度 0~360 → Pan 舵机 0~180
     * 声源 0°=前方 → Pan 90°（居中）
     * 声源 90°=右方 → Pan 0°（右转到底）
     * 声源 270°=左方 → Pan 180°（左转到底）
     * 声源 180°=后方 → 不处理（MG90S 只能前方半圆）
     */
    if (sound_angle >= 270 || sound_angle < 90) {
        /* 前方半圆：映射 270~360+0~90 → 180~90~0 */
        float mapped;
        if (sound_angle >= 270) {
            mapped = sound_angle - 270;  /* 0~90 */
            mapped = 180.0f - (mapped * 2.0f);  /* 180~0 */
        } else {
            mapped = 90.0f - (sound_angle * 2.0f);  /* 90~0 */
            /* 实际: 0°→90, 45°→0, 90°→-90 钳制 */
            if (mapped < 0) mapped = 0;
        }
        /* 简化方案：直接映射 -90~+90 → 0~180 */
        float relative = sound_angle;
        if (relative > 180) relative -= 360;
        /* relative: -180~180, 前方范围 -90~90 */
        float pan = 90.0f - relative;
        if (pan < 0) pan = 0;
        if (pan > 180) pan = 180;
        return pan;
    }
    /* 后方不追踪，返回当前角度 */
    return drv_servo_get_angle(SERVO_PAN);
}
```

- [ ] **Step 3: 更新 main/CMakeLists.txt 添加 servo 源文件**

在 SRCS 列表添加 `"drivers/drv_servo.c"`

- [ ] **Step 4: 更新 main.c 集成舵机测试**

在 `app_main` 中添加舵机初始化，在角度输出处添加舵机控制：

```c
/* 初始化舵机 */
drv_servo_init();

/* 在角度输出处 */
if (result.valid) {
    printf("angle: %.1f deg (conf=%.2f)\n", result.angle, result.confidence);
    drv_ws2812_show_angle(result.angle);
    float pan = drv_servo_angle_from_sound(result.angle);
    drv_servo_set_angle(SERVO_PAN, pan);
}
```

- [ ] **Step 5: 硬件验证 — 拍手后舵机应转向声源方向**

Run: `idf.py -p /dev/cu.usbmodem* flash monitor`
Expected: 在前方拍手时，Pan 舵机转向声源方向，精度±15°；灯环同步指示

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add MG90S servo driver with sound-to-angle mapping"
```

---

## Task 5: UART 板间通信

**Files:**
- Create: `PathFinder_Tracker/main/comm/tracker_protocol.h`
- Create: `PathFinder_Tracker/main/drivers/drv_uart_comm.h`
- Create: `PathFinder_Tracker/main/drivers/drv_uart_comm.c`
- Modify: `PathFinder_Tracker/main/CMakeLists.txt`
- Modify: `PathFinder_Tracker/main/main.c`

- [ ] **Step 1: 创建 tracker_protocol.h — 帧定义**

```c
#ifndef TRACKER_PROTOCOL_H
#define TRACKER_PROTOCOL_H

#include <stdint.h>

#define UART_FRAME_HEADER   0xAA
#define UART_FRAME_TAIL     0x55

/* 命令类型 */
typedef enum {
    CMD_ANGLE_DATA   = 0x01,  /* 声源角度数据 */
    CMD_TRACK_STATE  = 0x02,  /* 追踪状态 */
    CMD_FACE_INFO    = 0x03,  /* 人脸检测信息（Phase 3） */
} tracker_cmd_t;

/* 帧结构：[HEADER] [CMD] [LEN] [DATA...] [CRC8] [TAIL] */
#pragma pack(push, 1)
typedef struct {
    uint8_t  header;
    uint8_t  cmd;
    uint8_t  len;
    uint8_t  data[16];
    uint8_t  crc8;
    uint8_t  tail;
} tracker_frame_t;
#pragma pack(pop)

/* CRC8 计算（多项式 0x07） */
uint8_t tracker_crc8(const uint8_t *data, uint8_t len);

#endif /* TRACKER_PROTOCOL_H */
```

- [ ] **Step 2: 创建 drv_uart_comm.h**

```c
#ifndef DRV_UART_COMM_H
#define DRV_UART_COMM_H

#include "esp_err.h"
#include "tracker_protocol.h"
#include "tracker_config.h"

/**
 * 初始化 UART1（板间通信）
 */
esp_err_t drv_uart_init(void);

/**
 * 发送声源角度帧
 * angle: 0~360 度（0.1°精度，uint16）
 * valid: 角度是否有效
 */
esp_err_t drv_uart_send_angle(float angle, uint8_t valid);

/**
 * 发送追踪状态帧
 * state: tracker_state_t 枚举值
 */
esp_err_t drv_uart_send_state(uint8_t state);

/**
 * 组装并发送一帧
 */
esp_err_t drv_uart_send_frame(tracker_cmd_t cmd, const uint8_t *data, uint8_t data_len);

#endif /* DRV_UART_COMM_H */
```

- [ ] **Step 3: 创建 drv_uart_comm.c**

```c
#include "drv_uart_comm.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "drv_uart";

uint8_t tracker_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }
    return crc;
}

esp_err_t drv_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) return ret;

    ret = uart_param_config(UART_PORT_NUM, &cfg);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin(UART_PORT_NUM, UART_TX_GPIO, UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "UART%d ready: TX=GPIO%d RX=GPIO%d @%dbps",
             UART_PORT_NUM, UART_TX_GPIO, UART_RX_GPIO, UART_BAUD_RATE);
    return ESP_OK;
}

esp_err_t drv_uart_send_frame(tracker_cmd_t cmd, const uint8_t *data, uint8_t data_len)
{
    if (data_len > 16) return ESP_ERR_INVALID_ARG;

    tracker_frame_t frame = {0};
    frame.header = UART_FRAME_HEADER;
    frame.cmd = cmd;
    frame.len = data_len;
    if (data_len > 0 && data) {
        memcpy(frame.data, data, data_len);
    }

    /* CRC 覆盖 cmd + len + data */
    uint8_t crc_data[2 + 16];
    crc_data[0] = frame.cmd;
    crc_data[1] = frame.len;
    memcpy(crc_data + 2, frame.data, data_len);
    frame.crc8 = tracker_crc8(crc_data, 2 + data_len);
    frame.tail = UART_FRAME_TAIL;

    int written = uart_write_bytes(UART_PORT_NUM, &frame, sizeof(tracker_frame_t));
    return (written == sizeof(tracker_frame_t)) ? ESP_OK : ESP_FAIL;
}

esp_err_t drv_uart_send_angle(float angle, uint8_t valid)
{
    uint16_t angle_u16 = (uint16_t)(angle * 10);  /* 0.1° 单位 */
    uint8_t data[3];
    data[0] = angle_u16 & 0xFF;
    data[1] = (angle_u16 >> 8) & 0xFF;
    data[2] = valid;
    return drv_uart_send_frame(CMD_ANGLE_DATA, data, 3);
}

esp_err_t drv_uart_send_state(uint8_t state)
{
    return drv_uart_send_frame(CMD_TRACK_STATE, &state, 1);
}
```

- [ ] **Step 4: 更新 main/CMakeLists.txt 添加 UART 源文件**

在 SRCS 添加 `"drivers/drv_uart_comm.c"`，确保 REQUIRES 含 `esp_driver_uart`

- [ ] **Step 5: 更新 main.c 集成 UART 上报**

在初始化部分添加 `drv_uart_init()`，在角度输出处添加 UART 上报：

```c
if (result.valid) {
    printf("angle: %.1f deg (conf=%.2f)\n", result.angle, result.confidence);
    drv_ws2812_show_angle(result.angle);
    float pan = drv_servo_angle_from_sound(result.angle);
    drv_servo_set_angle(SERVO_PAN, pan);
    drv_uart_send_angle(result.angle, 1);
} else {
    /* 每50帧发送一次无效状态 */
    static int invalid_cnt = 0;
    if (++invalid_cnt >= 50) {
        drv_uart_send_angle(0, 0);
        invalid_cnt = 0;
    }
}
```

- [ ] **Step 6: 烧录验证 — 连接板子A 和 B 的 UART**

将板子B GPIO43(TX) → 板子A GPIO44(RX)，板子B GPIO44(RX) ← 板子A GPIO43(TX)，共地。

Run: `idf.py -p /dev/cu.usbmodem* flash monitor`
Expected: 板子B 串口输出角度数据，同时板子A 能通过 UART 收到帧（可在板子A侧加调试打印验证）

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat: add UART inter-board communication protocol"
```

---

## Task 6: 声源→舵机闭环优化

将各模块整合到完整状态机中，加入角度平滑滤波和舵机限速。

**Files:**
- Create: `PathFinder_Tracker/main/tracker_state_machine.h`
- Create: `PathFinder_Tracker/main/tracker_state_machine.c`
- Modify: `PathFinder_Tracker/main/main.c`
- Modify: `PathFinder_Tracker/main/CMakeLists.txt`

- [ ] **Step 1: 创建 tracker_state_machine.h**

```c
#ifndef TRACKER_STATE_MACHINE_H
#define TRACKER_STATE_MACHINE_H

#include "tracker_config.h"

/**
 * 状态机上下文
 */
typedef struct {
    tracker_state_t current_state;
    float   smoothed_angle;     /* 平滑后的角度 */
    float   last_valid_angle;   /* 最后有效角度 */
    int     invalid_count;      /* 连续无效帧计数 */
    int     search_count;       /* 搜索状态持续帧数 */
} tracker_ctx_t;

/**
 * 初始化状态机上下文
 */
void tracker_sm_init(tracker_ctx_t *ctx);

/**
 * 状态机单步处理
 * sound_angle: 声源角度（-1表示无效）
 * valid: 角度是否有效
 * 返回当前应执行的动作描述
 */
void tracker_sm_step(tracker_ctx_t *ctx, float sound_angle, bool valid);

#endif /* TRACKER_STATE_MACHINE_H */
```

- [ ] **Step 2: 创建 tracker_state_machine.c**

```c
#include "tracker_state_machine.h"
#include "drv_servo.h"
#include "drv_ws2812.h"
#include "drv_uart_comm.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "tracker_sm";

#define SMOOTH_ALPHA      0.3f   /* 指数平滑系数 */
#define MAX_INVALID_FRAMES 100   /* 连续无效帧阈值（~2秒） */
#define SEARCH_DURATION    250   /* 搜索状态持续帧数（~5秒） */
#define SERVO_STEP_MAX     3.0f  /* 单帧最大移动角度 */

void tracker_sm_init(tracker_ctx_t *ctx)
{
    ctx->current_state = TRACKER_STATE_IDLE;
    ctx->smoothed_angle = -1.0f;
    ctx->last_valid_angle = -1.0f;
    ctx->invalid_count = 0;
    ctx->search_count = 0;
}

static float angle_diff(float a, float b)
{
    float d = a - b;
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    return d;
}

void tracker_sm_step(tracker_ctx_t *ctx, float sound_angle, bool valid)
{
    switch (ctx->current_state) {

    case TRACKER_STATE_IDLE:
        drv_ws2812_clear();
        drv_ws2812_show();
        if (valid) {
            ctx->smoothed_angle = sound_angle;
            ctx->last_valid_angle = sound_angle;
            ctx->current_state = TRACKER_STATE_ACOUSTIC_TRACK;
            ESP_LOGI(TAG, "IDLE → ACOUSTIC_TRACK angle=%.1f", sound_angle);
        }
        break;

    case TRACKER_STATE_ACOUSTIC_TRACK:
        if (valid) {
            /* 指数平滑 */
            if (ctx->smoothed_angle < 0) {
                ctx->smoothed_angle = sound_angle;
            } else {
                float diff = angle_diff(sound_angle, ctx->smoothed_angle);
                ctx->smoothed_angle += diff * SMOOTH_ALPHA;
                if (ctx->smoothed_angle < 0) ctx->smoothed_angle += 360;
                if (ctx->smoothed_angle >= 360) ctx->smoothed_angle -= 360;
            }
            ctx->last_valid_angle = ctx->smoothed_angle;
            ctx->invalid_count = 0;

            /* 驱动灯环 + 舵机 */
            drv_ws2812_show_angle(ctx->smoothed_angle);

            /* 限速移动 */
            float target_pan = drv_servo_angle_from_sound(ctx->smoothed_angle);
            float current_pan = drv_servo_get_angle(SERVO_PAN);
            float delta = target_pan - current_pan;
            if (fabsf(delta) > SERVO_STEP_MAX) {
                delta = (delta > 0) ? SERVO_STEP_MAX : -SERVO_STEP_MAX;
            }
            drv_servo_set_angle(SERVO_PAN, current_pan + delta);

            /* UART 上报 */
            drv_uart_send_angle(ctx->smoothed_angle, 1);
            drv_uart_send_state(TRACKER_STATE_ACOUSTIC_TRACK);
        } else {
            ctx->invalid_count++;
            if (ctx->invalid_count >= MAX_INVALID_FRAMES) {
                ctx->current_state = TRACKER_STATE_SEARCH;
                ctx->search_count = 0;
                ESP_LOGI(TAG, "ACOUSTIC_TRACK → SEARCH");
            }
        }
        break;

    case TRACKER_STATE_SEARCH:
        ctx->search_count++;
        /* 回到最后已知方向 */
        if (ctx->last_valid_angle >= 0) {
            drv_ws2812_show_angle(ctx->last_valid_angle);
            float pan = drv_servo_angle_from_sound(ctx->last_valid_angle);
            drv_servo_set_angle(SERVO_PAN, pan);
        }
        if (valid) {
            ctx->smoothed_angle = sound_angle;
            ctx->current_state = TRACKER_STATE_ACOUSTIC_TRACK;
            ESP_LOGI(TAG, "SEARCH → ACOUSTIC_TRACK");
        } else if (ctx->search_count >= SEARCH_DURATION) {
            /* 搜索超时，回到居中 */
            drv_servo_set_angle(SERVO_PAN, 90.0f);
            drv_ws2812_clear();
            drv_ws2812_show();
            ctx->current_state = TRACKER_STATE_IDLE;
            ESP_LOGI(TAG, "SEARCH → IDLE (timeout)");
        }
        break;

    case TRACKER_STATE_FACE_TRACK:
        /* Phase 3 实现 */
        break;
    }
}
```

- [ ] **Step 3: 更新 main/CMakeLists.txt 添加状态机**

在 SRCS 添加 `"tracker_state_machine.c"`

- [ ] **Step 4: 更新 main.c 整合状态机**

```c
#include "tracker_state_machine.h"

static tracker_ctx_t s_tracker_ctx;

void app_main(void)
{
    printf("[%s] PathFinder_Tracker Phase 1-2 starting\n", TAG);

    /* 初始化外设 */
    drv_ws2812_init();
    drv_servo_init();
    drv_uart_init();

    esp_err_t ret = drv_es7210_init();
    if (ret != ESP_OK) {
        printf("[%s] ES7210 init FAILED: %s\n", TAG, esp_err_to_name(ret));
        return;
    }

    sound_localizer_init(ES7210_SAMPLE_RATE);
    sound_localizer_set_threshold(-2.7f);

    /* 初始化状态机 */
    tracker_sm_init(&s_tracker_ctx);

    /* 主循环 */
    float mic_data[4][256];
    while (1) {
        if (drv_es7210_read(mic_data) == ESP_OK) {
            localization_result_t result = sound_localizer_compute(mic_data);
            tracker_sm_step(&s_tracker_ctx, result.angle, result.valid);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

- [ ] **Step 5: 完整系统验证 — 端到端测试**

Run: `idf.py -p /dev/cu.usbmodem* flash monitor`

验证场景：
1. **待机**：上电后舵机居中（90°），灯环熄灭
2. **拍手追踪**：在左侧拍手 → 灯环指向左侧 → 舵机缓慢转向左侧
3. **持续追踪**：在不同方向拍手 → 舵机跟随
4. **丢失搜索**：停止拍手 → 2秒后进入 SEARCH → 5秒后回到居中
5. **UART上报**：板子A 串口能收到角度帧（如板子A 已加接收代码）

- [ ] **Step 6: 延迟测量**

用手机秒表计时：从拍手到舵机开始转动，应 < 300ms

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat: complete acoustic tracking state machine with servo control"
```

---

## Task 7: 供电验证文档

这一步不是代码任务，而是硬件验证清单，确保供电方案安全。

- [ ] **Step 1: 准备硬件清单**

| 器件 | 规格 | 数量 |
|------|------|------|
| 5V 电源适配器 | 5V/5A, DC 5.5/2.1mm 或 USB-C PD | 1 |
| ESP32-S3 N16R8 开发板 | 16MB Flash, 8MB PSRAM | 1 |
| AcousticEye 模块 | ES7210 + WS2812 ×36 | 1 |
| OV2640 摄像头模块 | DVP 接口 | 1（Phase 3 接入） |
| MG90S 舵机 | 180°, 4.8-6V | 2 |
| MAX98357A 功放 | I2S 输入 | 1（Phase 4 接入） |
| 杜邦线 | 母对母/公对母 | 若干 |

- [ ] **Step 2: 供电接线确认**

- [ ] ESP32-S3 VIN/5V → 电源适配器 5V 输出
- [ ] 舵机 VCC（红线）→ 电源适配器 5V（**独立于 ESP32 的 5V 输入端子**，但**共地**）
- [ ] ESP32-S3 GND → 电源适配器 GND
- [ ] 舵机 GND（棕线）→ 电源适配器 GND
- [ ] **万用表测量**：5V 端子空载电压 4.95-5.05V，接入后不低于 4.8V

- [ ] **Step 3: 提交供电验证记录**

```bash
git commit --allow-empty -m "docs: power supply verified - 5V/5A three-rail common-ground"
```

---

## Phase 1-2 退出条件

全部满足后方可进入 Phase 3（人脸检测）：

- [ ] ES7210 四通道音频正常读取（串口有 RMS/peak 数据）
- [ ] 拍手时输出有效角度（0~360°），精度±15°
- [ ] WS2812 灯环指向声源方向
- [ ] Pan 舵机跟随声源转动，限速无抖动
- [ ] 声源消失后：2秒进入 SEARCH → 5秒后回居中 → IDLE
- [ ] UART 帧正确发送（板子A 可解析）
- [ ] 连续运行30分钟无崩溃/看门狗复位
