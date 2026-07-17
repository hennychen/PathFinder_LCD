# PathFinder Tracker：多模态智能追踪云台系统设计

> 日期：2026-07-16  
> 状态：已批准（方案C）  
> 硬件：ESP32-S3-WROOM-1 N16R8 + OV2640 + AcousticEye + MG90S×2 + MAX98357A

## 1. 系统概述

PathFinder Tracker 是一个基于 ESP32-S3 的多模态智能追踪云台系统，实现"声源定位 → 舵机粗调 → 人脸检测 → PID精调"的完整闭环，并集成 xiaozhi AI 语音助手能力。

### 1.1 双板架构

系统由两块独立的 ESP32-S3 组成，通过 UART 通信：

| 角色 | 硬件 | 职责 |
|------|------|------|
| **板子A**（TK021F2699） | ESP32-S3, 16MB Flash, 8MB Octal PSRAM | LCD显示 + 传感器 + LVGL UI + BLE配网（**保持现状不动**） |
| **板子B**（新购入 N16R8） | ESP32-S3-WROOM-1, 16MB Flash, 8MB Octal PSRAM | 声源定位 + 人脸检测 + 舵机控制 + xiaozhi AI语音 |

### 1.2 为什么 AcousticEye 不能接入板子A

板子A的 RGB LCD 已占用 GPIO 2/4/5/6/9/10/13 等引脚，与 ES7210 的 I2S/I2C 信号线**物理重叠**（7个GPIO硬冲突）。此外，ES7210 的 I2S DMA 运行时在 GPIO4/5/9/10 产生 ~3MHz 高频翻转，通过杜邦线耦合干扰 CH340 UART，导致烧录失败。这是项目历史中已验证的约束。

## 2. 完整引脚分配（板子B）

### 2.1 OV2640 摄像头（64074模块）— DVP 并行接口（14 GPIO）

200万像素 OV2640 CMOS 摄像头模块（料号 64074），支持 JPEG 输出。通过 **DVP 并行接口 + SCCB I2C** 与 ESP32-S3 连接，使用 LCD_CAM 外设控制器驱动。

**DVP 数据总线（8 GPIO）**

| 信号 | GPIO | 方向 | 说明 |
|------|------|------|------|
| D0 | 11 | 摄像头→MCU | 数据位0 |
| D1 | 9 | 摄像头→MCU | 数据位1 |
| D2 | 8 | 摄像头→MCU | 数据位2 |
| D3 | 10 | 摄像头→MCU | 数据位3 |
| D4 | 12 | 摄像头→MCU | 数据位4 |
| D5 | 18 | 摄像头→MCU | 数据位5 |
| D6 | 17 | 摄像头→MCU | 数据位6 |
| D7 | 16 | 摄像头→MCU | 数据位7 |

**DVP 同步与时钟（4 GPIO）**

| 信号 | GPIO | 方向 | 说明 |
|------|------|------|------|
| VSYNC | 6 | 摄像头→MCU | 帧同步信号 |
| HREF | 7 | 摄像头→MCU | 行参考信号 |
| PCLK | 13 | 摄像头→MCU | 像素时钟 |
| XCLK | 15 | MCU→摄像头 | 外部时钟（16-20MHz） |

**SCCB I2C 配置总线（2 GPIO）**

| 信号 | GPIO | 方向 | 说明 |
|------|------|------|------|
| SIOD (SCCB SDA) | 4 | 双向 | I2C0 数据线（独立于 ES7210 的 I2C1） |
| SIOC (SCCB SCL) | 5 | 双向 | I2C0 时钟线（独立于 ES7210 的 I2C1） |

**电源与控制（非 GPIO）**

| 引脚 | 连接 | 说明 |
|------|------|------|
| VCC | 3.3V | 模块供电（~100mA，禁接 5V） |
| GND | GND | 公共地 |
| RESET | 3.3V 或悬空 | 复位（低有效，正常工作拉高） |
| PWDN | GND 或悬空 | 掉电控制（高=休眠，低=工作） |

> **GPIO 13 冲突说明**：PCLK 占用 GPIO 13，这正是 AcousticEye 的 PA_EN 必须从 GPIO 13 迁移到 GPIO 45 的原因。SCCB 使用 I2C0（GPIO 4/5），与 ES7210 的 I2C1（GPIO 38/39）完全隔离，避免总线冲突。

### 2.2 AcousticEye 模块 — ES7210 + WS2812（8 GPIO）

I2C1 控制器配置 ES7210 寄存器，I2S0 控制器传输音频流，RMT 驱动 WS2812 灯环。PA_EN 用于板级音频硬件使能。

| 信号 | GPIO | 说明 |
|------|------|------|
| ES7210 I2C SDA | 38 | I2C1 数据线 |
| ES7210 I2C SCL | 39 | I2C1 时钟线 |
| I2S0 MCLK | 42 | 音频主时钟 |
| I2S0 BCLK | 41 | 位时钟 |
| I2S0 WS | 40 | 字选择 |
| I2S0 DIN | 21 | 四通道音频数据输入 |
| WS2812 DIN | 48 | RGB 灯环数据线（原板 GPIO2，因 I2S1 WS 占用迁移） |
| PA_EN（板级音频使能） | 45 | 输出高电平使能 ES7210/音频电路（原板 GPIO13，因 OV2640 PCLK 占用迁移） |

> **PA_EN 时序要求**：必须在 ES7210 I2C 寄存器访问**之前**拉高，否则芯片未上电时写 `ES7210_RESET_REG00` 会失败。代码中 `initI2SMics()` 的调用顺序已满足此要求。

### 2.3 MG90S 舵机 — LEDC PWM（2 GPIO）

| 信号 | GPIO |
|------|------|
| Pan（水平） | 14 |
| Tilt（垂直） | 47 |

LEDC PWM 50Hz（20ms周期），1-2ms 脉宽控制。MG90S 需外部 5V 供电。

### 2.4 xiaozhi AI TTS 输出 — MAX98357A 功放 I2S1（3 GPIO）

| 信号 | GPIO | MAX98357A 引脚 |
|------|------|----------------|
| I2S1 BCLK | 1 | BCLK（位时钟） |
| I2S1 WS | 2 | LRC / WS（字选择） |
| I2S1 DOUT | 3 | DIN（数据输入） |

语音输入复用 ES7210 的 CH0 通道（I2S0 共享），无需额外麦克风。

> **MAX98357A 供电**：VIN 接 5V（共用 ESP32 的 5V/VIN 总线），GND 与全系统共地，GAIN 悬空（默认 9dB），SD 悬空（默认开启）。GPIO 2 同时是 I2S1 WS，这正是 WS2812 不能再用 GPIO 2 的原因。

### 2.5 UART 通信 + USB 编程（4 GPIO）

| 信号 | GPIO |
|------|------|
| UART TX → 板子A | 43 |
| UART RX ← 板子A | 44 |
| USB D-（编程） | 19 |
| USB D+（编程） | 20 |

### 2.6 引脚汇总

- 总计：**31 GPIO**，全部唯一，零冲突
- GPIO 26-33：Octal PSRAM 占用，不可用
- 剩余备用：0, 22, 23, 24, 25, 35, 36, 37, 46

### 2.7 AcousticEye 原板 → 板子B 引脚迁移对照

AcousticEye 声源定位板原独立运行时使用一组默认引脚，迁移到板子B后因与其他外设冲突，部分引脚需重新映射。

| AcousticEye 信号 | 原板 GPIO | 板子B GPIO | 迁移原因 |
|------------------|-----------|------------|----------|
| ES7210 I2C SDA | 10 | **38** | 板子B 统一分配 I2C1 |
| ES7210 I2C SCL | 6 | **39** | 板子B 统一分配 I2C1 |
| I2S0 MCLK | 9 | **42** | 板子B 统一分配 I2S0 |
| I2S0 BCLK | 5 | **41** | 板子B 统一分配 I2S0 |
| I2S0 WS | 4 | **40** | 板子B 统一分配 I2S0 |
| I2S0 DIN | 8 | **21** | 板子B 统一分配 I2S0 |
| WS2812 DIN（RGB 灯环） | 2 | **48** | GPIO2 被 I2S1 WS（MAX98357A）占用 |
| PA_EN（板级音频使能） | 13 | **45** | GPIO13 被 OV2640 PCLK 占用 |

**代码修改要点**：
- `ws2812.h` 中 `LED_STRIP_GPIO_PIN` 从 `2` 改为 `48`
- `calc_direction.c` 中 PA_EN 从 `GPIO_NUM_13` 改为 `GPIO_NUM_45`
- ES7210 I2C/I2S 默认引脚按上表全部更新

## 3. 供电方案

### 3.1 电源规格

**5V / 5A 直流电源适配器**（推荐 USB-C PD 或 DC 5.5/2.1mm 圆口）

### 3.2 电流预算

| 负载 | 电流 |
|------|------|
| 2× MG90S 舵机（堵转） | ~2.0A |
| ESP32-S3 + PSRAM | ~250mA |
| OV2640 摄像头 | ~100mA |
| ES7210 × 4通道 | ~20mA |
| WS2812 × 36颗（全白） | ~2.16A |
| MAX98357A 功放 | ~200mA |
| **峰值总计** | **~4.7A** |

### 3.3 供电分配（三路独立 + 共地）

1. **ESP32-S3 5V/VIN**：板载 LDO 降到 3.3V 供芯片、OV2640、ES7210
2. **舵机独立 5V 总线**：直接接 MG90S VCC（红线），不经过 ESP32
3. **WS2812 独立 5V**：如全亮功耗大，建议独立供电

**所有回路必须共地（GND）。舵机绝不接 ESP32 的 3.3V。**

## 4. 多模态追踪控制流程

### 4.1 四状态机

```
IDLE（待机）
  │ ▼ 检测到声源（能量 > 阈值）
ACOUSTIC_TRACK（声源粗调）
  │ ▼ 声源方向稳定 + 舵机到位 + 检测到人脸
FACE_TRACK（人脸精调）
  │ ▼ 人脸丢失（连续N帧未检测到）
SEARCH（搜索回退）→ 回到最后已知声源方向 → 持续5秒 → ACOUSTIC_TRACK → IDLE
```

### 4.2 双核任务分配

**Core 0（PRO_CPU）— 音频 + 舵机 + 语音AI**
- I2S0 DMA 采集 ES7210 四通道音频
- MUSIC / SRP-PHAT 声源定位算法
- ES7210 CH0 复用为 xiaozhi 语音输入
- xiaozhi 唤醒检测 + ASR + LLM 对话（Wi-Fi）
- I2S1 TTS → MAX98357A 播放
- 角度 → 舵机位置映射 + LEDC PWM 输出
- WS2812 灯环指示状态
- UART 发送状态给板子A

**Core 1（APP_CPU）— 视觉**
- DVP 接口采集 OV2640 图像帧
- ESP-DL 人脸检测推理（PSRAM）
- 人脸框偏移 → PID → 舵机微调指令
- 帧率自适应：待机 1fps / 追踪 8-10fps

### 4.3 舵机控制策略

**声源阶段（粗调）**
- 角度范围：-90° ~ +90°（水平）
- 直接位置控制：角度 → 脉宽映射
- 大角度快速移动，无 PID
- WS2812 灯环同步指向声源方向

**人脸阶段（精调）**
- PID 控制器：(Δx → Pan, Δy → Tilt)
- 死区：±10 像素内不动作（防抖）
- 限速：单帧最大移动 2°（防抖动）

### 4.4 延迟预期

- 声源计算 ~50ms + 舵机响应 ~200ms = **~250ms**（粗调）
- 人脸推理 ~100ms + PID 控制 ~20ms = **~120ms**（精调，8-10 FPS）

## 5. 板间通信协议

板子B 通过 UART 向板子A 上报状态数据，板子A 在 LCD 显示追踪状态。

### 5.1 UART 帧格式（115200bps）

沿用现有协议基础：帧头 0xAA、帧尾 0x55、CRC8 校验。

| CMD | 含义 | 数据字段 |
|-----|------|----------|
| 0x01 | 声源角度数据 | ANGLE(uint16, 0.1°) + VALID(uint8) |
| 0x02 | 追踪状态（新增） | STATE(uint8: 0=IDLE, 1=ACOUSTIC, 2=FACE, 3=SEARCH) |
| 0x03 | 人脸检测信息（新增） | CONFIDENCE(uint8) + BOX_SIZE(uint16) |

### 5.2 物理连接

板子A GPIO43(TX) → 板子B GPIO44(RX)  
板子A GPIO44(RX) ← 板子B GPIO43(TX)  
两板共地

## 6. 软件架构

### 6.1 项目结构（PathFinder_Tracker）

```
PathFinder_Tracker/
├── CMakeLists.txt
├── sdkconfig.defaults          # PSRAM Octal + 摄像头 + I2S 双总线配置
├── partitions.csv              # 4MB app + 4MB model + 4MB spiffs
├── main/
│   ├── main.c                  # 入口：初始化所有外设，启动双核任务
│   ├── tracker_state_machine.h # 4状态机定义
│   ├── tracker_state_machine.c
│   ├── drivers/
│   │   ├── drv_es7210.c/h      # ES7210 I2C配置 + I2S0采集（从AcousticEye迁移）
│   │   ├── drv_ov2640.c/h      # OV2640 DVP初始化（ESP-IDF esp_camera）
│   │   ├── drv_servo.c/h       # LEDC PWM 舵机驱动
│   │   ├── drv_ws2812.c/h      # RMT WS2812 灯环
│   │   ├── drv_tts_amp.c/h     # I2S1 MAX98357A TTS输出
│   │   └── drv_uart_comm.c/h   # UART协议收发
│   ├── audio/
│   │   ├── sound_localizer.c/h # MUSIC/SRP-PHAT 声源定位算法
│   │   └── audio_config.h      # 采样率、帧长、通道数常量
│   ├── vision/
│   │   ├── face_detector.c/h   # ESP-DL 人脸检测封装
│   │   ├── face_tracker.c/h    # PID控制 + 死区 + 限速
│   │   └── model_define.h      # ESP-DL 模型参数
│   ├── voice/                  # xiaozhi AI 模块
│   │   ├── xiaozhi_engine.c/h  # 唤醒 + ASR + LLM + TTS 编排
│   │   └── voice_config.h      # Wi-Fi配置、LLM API端点
│   └── comm/
│       └── tracker_protocol.h  # UART帧定义
└── components/
    ├── esp-dl/                 # ESP-DL 人脸检测推理库
    └── es7210/                 # ES7210 驱动（从AcousticEye复制）
```

### 6.2 关键配置（sdkconfig.defaults）

```ini
# PSRAM: Octal 模式 80MHz
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# CPU 240MHz
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# 摄像头
CONFIG_CAMERA_OV2640=y

# Flash 16MB
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# Wi-Fi（xiaozhi AI 需要）
CONFIG_ESP_WIFI_ENABLED=y
```

## 7. 分阶段实施计划

### Phase 1：硬件接线 + 驱动验证（1-2 天）

**目标**：所有外设在新 ESP32-S3 上独立验证通过。

- [ ] 按引脚表焊接/插接所有模块（含 MAX98357A）
- [ ] 5V 独立电源供电，万用表验证电压
- [ ] ES7210：I2C 扫描到器件地址，I2S0 读到四通道数据
- [ ] OV2640：DVP 拍照输出到串口
- [ ] 舵机：LEDC PWM 驱动 Pan/Tilt 左右上下转动
- [ ] WS2812：点亮指定颜色
- [ ] MAX98357A：I2S1 输出正弦波测试音
- [ ] UART：与板子A 收发测试帧
- [ ] USB 烧录：确认 N16R8 的 boot 流程

**退出条件**：每个外设都有独立的 test 例程通过。

### Phase 2：声源定位 → 舵机闭环（2-3 天）

**目标**：拍手/说话 → 云台自动转向声源。

- [ ] 迁移 AcousticEye 声源算法到新项目
- [ ] 角度 → 舵机脉宽映射函数标定
- [ ] WS2812 灯环指向声源方向
- [ ] UART 上报角度给板子A
- [ ] Core0 独占运行，不接摄像头
- [ ] 延迟测试：声源 → 舵机响应 < 300ms

**退出条件**：拍手后云台在 300ms 内指向声源方向，±15° 精度。

### Phase 3：人脸检测 → 精细追踪（3-5 天）

**目标**：人脸出现在画面 → 云台 PID 微调追踪。

- [ ] ESP-DL 人脸检测模型部署到 PSRAM
- [ ] OV2640 QVGA 采集 → 推理 → 人脸框
- [ ] PID 控制器实现（Core1）
- [ ] 死区 ±10px + 限速 2°/帧
- [ ] 与 Phase 2 的声源状态机对接
- [ ] 双核联调：Core0 音频 + Core1 视觉

**退出条件**：人脸追踪 8-10 FPS，延迟 < 150ms，移动时保持锁定。

### Phase 4：xiaozhi AI 语音集成（2-3 天）

**目标**：语音唤醒 → LLM 对话 → TTS 播报，且与追踪系统共存。

- [ ] 移植 xiaozhi-esp32 核心框架
- [ ] ES7210 CH0 复用为语音输入（验证与声源定位的时分复用）
- [ ] I2S1 → MAX98357A TTS 播放
- [ ] Wi-Fi 连接 + LLM API 对接
- [ ] 语音唤醒词与声源触发的优先级协调
- [ ] 内存优化：确保 xiaozhi + ESP-DL 共存不 OOM

**退出条件**：语音唤醒后可对话，同时追踪功能不受影响。

### Phase 5：全链路联调 + UI 联动（1-2 天）

**目标**：完整体验 — 说话 → 转头 → 锁定 → 追踪 → 语音交互 → 显示。

- [ ] 状态机 4 状态完整流转测试
- [ ] 人脸丢失 → 搜索 → 回退逻辑验证
- [ ] UART 上报追踪状态给板子A
- [ ] 板子A LCD 显示追踪状态胶囊
- [ ] WS2812 状态指示（待机/追踪/搜索/语音）
- [ ] 长时间稳定性测试（2小时+）

**退出条件**：端到端体验流畅，无崩溃、无舵机抖动、无漏检。

**总工期估算：9-15 天**（含调试和返工缓冲）

## 8. 关键风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| ESP-DL 人脸检测 FPS 不达标 | 追踪延迟大 | 降低分辨率到 QQVGA，或减少检测频率到 5fps |
| I2S0 和 I2S1 双 DMA 争抢 PSRAM 带宽 | 音频断续 | I2S0 buffer 用内部 SRAM，仅摄像头帧放 PSRAM |
| 舵机抖动影响摄像头画面 | 检测不稳定 | 死区 + 限速 + 舵机供电独立稳压 |
| xiaozhi + ESP-DL 内存不足 | OOM 崩溃 | 模型量化压缩，限制 xiaozhi 对话缓冲区 |
| ES7210 CH0 复用语音与声源定位冲突 | 功能相互干扰 | 时分复用：声源定位帧间空闲期提取语音数据 |
