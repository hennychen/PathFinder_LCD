# AcousticEye 四麦克风声源定位系统

## 系统架构

```
ES7210 TDM 4-ch ADC
    │
    ├─ CH0 (MIC1) = 右     ─┐
    ├─ CH1 (MIC2) = 左     ─┤ X轴 GCC-PHAT → delay_x
    ├─ CH2 (MIC3) = 前     ─┤ Y轴 GCC-PHAT → delay_y
    └─ CH3 (MIC4) = 后     ─┘
                             │
              atan2(delay_y, delay_x) → 0~360°
                             │
              WS2812 LED 环指示方向
```

### 4 麦克风物理布局

```
         CH2 (前)
          |
 CH1(左) ─┼─ CH0 (右)
          |
         CH3 (后)
```

| 通道 | 位置 | 轴 | GCC-PHAT 对 |
|------|------|-----|-------------|
| CH0 | 右 (MIC1) | X轴A端 | CH0 ↔ CH1 |
| CH1 | 左 (MIC2) | X轴B端 | CH0 ↔ CH1 |
| CH2 | 前 (MIC3) | Y轴A端 | CH2 ↔ CH3 |
| CH3 | 后 (MIC4) | Y轴B端 | CH2 ↔ CH3 |

### 角度定义（atan2 双轴）

| 角度 | 方向 | LED 索引 |
|------|------|----------|
| 0° | 正右 (X+) | LED[0] |
| 90° | 正前 (Y+) | LED[9] |
| 180° | 正左 (X-) | LED[18] |
| 270° | 正后 (Y-) | LED[27] |

## 硬件配置

### I2S 混合模式（STD TX + TDM RX）

ESP32-S3 的单个 I2S 控制器同时驱动两个 codec：

| 方向 | 模式 | Codec | BCLK 计算 |
|------|------|-------|-----------|
| TX (输出) | 标准 I2S | ES8311 DAC | 24000 × 2 × 32 = 1.536 MHz |
| RX (输入) | TDM 4-slot | ES7210 ADC | 24000 × 4 × 16 = 1.536 MHz |

两者 BCLK 完全匹配（1.536 MHz），可稳定共享同一 I2S 控制器。

### ES7210 PGA 增益

`input_gain_ = 37.5`（GAIN_37_5DB，ES7210 最大增益）

### WS2812 LED 环

- GPIO48（与 BUILTIN_LED 共用）
- 36 颗 WS2812，红色指示
- `GetLed()` 必须返回 `NoLed` 以避免 RMT 冲突

## 软件数据流

```
ES7210 TDM 4ch → esp_codec_dev_read()
    │
    ├─ 提取 CH0 → 语音框架（小智唤醒词）
    │
    └─ 完整 4ch → sound_localizer_feed()
                      │
                      ├─ 环形缓冲区 (256 samples/frame)
                      │
                      └─ 后台任务 (50Hz)
                            │
                            ├─ L1: 能量门限 + 自适应噪声门限
                            ├─ 去直流 + Hann 窗
                            ├─ X轴 GCC-PHAT (CH0↔CH1)
                            ├─ Y轴 GCC-PHAT (CH2↔CH3)
                            ├─ atan2 → 0~360°
                            ├─ L2: GCC 可信度过滤
                            └─ L3: 角度稳定滤波器
                                      │
                                      └─ → LED 环 + 舵机追踪
```

## 三级过滤体系

### L1: 能量门限（固定 + 自适应）

| 参数 | 值 | 说明 |
|------|-----|------|
| `SL_MIN_ACTIVITY` | -2.5 | log10(RMS) > -2.5，即 RMS > 0.003 |
| `SL_NOISE_FLOOR_INIT` | 0.002 | 初始底噪 RMS 估计 |
| `SL_DETECT_RATIO` | 3.0 | 检测阈值 = noise_floor × 3 |
| `SL_NOISE_FLOOR_ALPHA_DOWN` | 0.05 | RMS < floor 时快速追踪 |
| `SL_NOISE_FLOOR_ALPHA_UP` | 0.003 | RMS > floor 时极慢上升 |
| `SL_NOISE_FLOOR_UPDATE_MAX` | 1.5 | RMS < floor × 1.5 时才更新 |

**关键设计：不对称噪声门限更新**

```
rms ≤ floor        → alpha = 0.05  (快速下降，适应环境变安静)
floor < rms < 1.5×  → alpha = 0.003 (极慢上升，防声音间隙污染)
rms ≥ 1.5× floor   → alpha = 0     (有声音，不更新)
```

### L2: GCC 可信度

| 参数 | 值 | 说明 |
|------|-----|------|
| `SL_MIN_PEAK_GCC` | 0.04 | GCC 主峰值最低要求 |
| `SL_MIN_PEAK_RATIO` | 1.5 | 主峰/次峰比值（底噪 ≈1.2） |
| `SL_MIN_DIR_VECTOR` | 0.05 | 方向向量幅度 √(dx² + dy²) |

### L3: 角度稳定滤波器

| 参数 | 值 | 说明 |
|------|-----|------|
| `SL_ANGLE_WINDOW` | 6 | 滑动窗口大小 |
| `SL_MIN_VALID_COUNT` | 3 | 至少 3 帧一致才输出 |
| `SL_MAX_SPREAD_DEG` | 20° | 窗口内最大角度散布 |
| `SL_ANGLE_SMOOTH_ALPHA` | 0.30 | 指数平滑系数 |

## 关键文件

| 文件 | 职责 |
|------|------|
| `sound_localizer.h` | 配置常量、阈值、通道映射 |
| `sound_localizer.c` | GCC-PHAT 算法、三级过滤、角度估算 |
| `box_audio_codec.cc` | I2S TDM 配置、4 通道读取与分发 |
| `pathfinder_tracker_board.cc` | 硬件初始化、LED 驱动任务 |
| `led_ring.c` | WS2812 36 LED 环驱动 |
| `config.h` | GPIO 引脚定义 |

## 实测数据

### 静默环境（底噪）

| 指标 | 值 |
|------|-----|
| RMS | 0.0014 |
| activity (log10) | -2.85 |
| GCC peak | 0.03 ~ 0.12 |
| GCC ratio | ~1.2 |
| noise_floor (收敛后) | 0.0013 ~ 0.0015 |
| rms_ratio | 0.8 ~ 1.2x |

### 有声音检测

| 指标 | 值 |
|------|-----|
| 说话 RMS | 0.009 ~ 0.06 |
| 说话 activity | -2.0 ~ -1.2 |
| 说话 rms_ratio | 5 ~ 20x |
| 角度覆盖 | 0 ~ 360° 全方向 |
