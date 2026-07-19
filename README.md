# PathFinder_LCD — 多模态智能车载系统

## 项目概述

PathFinder_LCD 是一个**三模块多模态智能车载系统**，集成声源定位、人脸追踪、动态表情、环境感知与 AI 语音能力：

| 模块 | 硬件 | 固件/应用 | 核心能力 |
|------|------|-----------|----------|
| **板 A（EMOTE）** | TK021F2699 (ESP32-S3) | PathFinder_EMOTE | 480×480 圆形屏 UI、传感器采集、BLE 通信、表情引擎 |
| **板 B（Tracker）** | ESP32-S3 N16R8 | PathFinder_Tracker | 声源定位、人脸追踪、舵机云台、Mesh 组网 |
| **Dashboard** | Android 手机 | PathFinder_Dashboard (Flutter) | BLE 实时可视化、数据持久化、历史回溯 |

---

## 系统架构

### 三模块总体架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       板 A — PathFinder_EMOTE (TK021F2699)                   │
│                                                                             │
│  ┌──── 传感器层 ────┐  ┌──── UI 层 ──────────┐  ┌──── 通信层 ────────┐     │
│  │ AHT20 温湿度     │  │ ST7701S 480×480 LCD  │  │ BLE GATT (C2/C3/C4)│     │
│  │ BMP280 气压海拔  │  │ CST3530 触摸屏       │  │ Wi-Fi STA/Mesh ROOT│     │
│  │ MPU6050 IMU      │  │ LVGL 8.3 渲染引擎    │  │ UART → 板 B        │     │
│  │ GUVA-S12SD UV    │  │ EAF 表情动画播放器    │  │ ESP-NOW 广播       │     │
│  └──────────────────┘  │ 飞行仪表盘           │  └────────────────────┘     │
│                         └─────────────────────┘                             │
└──────────────────────────────┬──────────────────────────────────────────────┘
                               │ Mesh / ESP-NOW / UART
                               ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     板 B — PathFinder_Tracker (N16R8)                        │
│                                                                             │
│  ┌──── 音频层 ──────┐  ┌──── 视觉层 ────────┐  ┌──── 执行层 ────────┐     │
│  │ ES7210 4ch 麦克风 │  │ OV2640 DVP 摄像头  │  │ 2×MG90S 舵机云台   │     │
│  │ GCC-PHAT 声源定位 │  │ ESP-DL 人脸检测     │  │ Pan (水平) + Tilt  │     │
│  │ I2S TDM 48kHz    │  │ Core-1 独立任务     │  │ PWM 50Hz 控制      │     │
│  └──────────────────┘  └─────────────────────┘  └────────────────────┘     │
│                                                                             │
│  ┌──── 通信层 ──────┐  ┌──── 指示层 ────────┐  ┌──── AI (预留) ─────┐     │
│  │ Mesh CHILD 节点   │  │ WS2812 × 36 灯环   │  │ MAX98357A TTS 输出 │     │
│  │ ESP-NOW 广播      │  │ 声源方向可视化       │  │ xiaozhi AI 语音    │     │
│  └──────────────────┘  └─────────────────────┘  └────────────────────┘     │
└─────────────────────────────────────────────────────────────────────────────┘
                               │ BLE GATT Notify
                               ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                   PathFinder_Dashboard (Flutter Android App)                 │
│                                                                             │
│  ┌──── BLE 通信层 ────┐  ┌──── 数据持久层 ────┐  ┌──── UI 可视化层 ───┐   │
│  │ ReactiveBleService  │  │ Drift SQLite ORM   │  │ Environment Screen │   │
│  │ BLE Codec 编解码     │  │ Env/Motion/Emote   │  │ Motion Screen      │   │
│  └─────────────────────┘  │ CSV Export          │  │ Emote Screen       │   │
│                            └─────────────────────┘  │ History Screen     │   │
│                                                     └────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 板间通信拓扑

```
板 A (EMOTE/ROOT)  ◄──── Mesh / ESP-NOW ────►  板 B (Tracker/CHILD)
       │                                                │
       │ BLE GATT                                       │ UART (115200)
       ▼                                                │ (备用直连)
  Flutter App                                      声源角度/人脸坐标
```

- **主链路**：ESP-WIFI-MESH（板 A = ROOT 连接路由器，板 B = CHILD 加入 Mesh）
- **备用链路**：UART GPIO19/GPIO20 @ 115200bps
- **消息类型**：角度数据、追踪状态、人脸信息、舵机控制、心跳

---

## 硬件接线图

### 板 A — PathFinder_EMOTE (TK021F2699)

**LCD RGB 面板（ST7701S，480×480）**

```
ESP32-S3                        ST7701S LCD (480×480)
┌──────────┐                   ┌──────────────────┐
│  GPIO  0 ├──── R3 ──────────►│ R3               │
│  GPIO  2 ├──── PCLK ────────►│ PCLK             │
│  GPIO  4 ├──── B0 ──────────►│ B0               │
│  GPIO  5 ├──── B1 ──────────►│ B1               │
│  GPIO  6 ├──── B2 ──────────►│ B2               │
│  GPIO  7 ├──── B3 ──────────►│ B3               │
│  GPIO  9 ├──── G4 ──────────►│ G4               │
│  GPIO 10 ├──── G5 ──────────►│ G5               │
│  GPIO 11 ├──── G6 ──────────►│ G6               │
│  GPIO 15 ├──── B4 ──────────►│ B4               │
│  GPIO 16 ├──── G0 ──────────►│ G0               │
│  GPIO 17 ├──── G1 ──────────►│ G1               │
│  GPIO 18 ├──── G3 ──────────►│ G3               │
│  GPIO 21 ├──── R4 ──────────►│ R4               │
│  GPIO 41 ├──── HSYNC ───────►│ HSYNC            │
│  GPIO 42 ├──── DE ──────────►│ DE               │
│  GPIO 46 ├──── VSYNC ───────►│ VSYNC            │
│  GPIO 45 ├──── R1 ──────────►│ R1               │
│  GPIO 47 ├──── R2 ──────────►│ R2               │
│  GPIO 48 ├──── G7 ──────────►│ G7               │
│  GPIO  1 ├──── SPI CS ──────►│ CS (初始化配置)   │
└──────────┘                   └──────────────────┘
```

**I2C 总线（复用触摸屏 I2C-0，100kHz）**

> ⚠️ **历史教训**：原计划使用 GPIO12(SCL)/GPIO14(SDA) 作为独立传感器 I2C-1 总线，
> 但 TK021F2699 板子上 **GPIO12/14 被 LCD RGB 信号间接占用**，接入传感器会导致
> **LCD 黑屏**（实测验证）。已改为传感器与触摸屏**复用同一 I2C-0 总线**（GPIO13/20），
> 各传感器地址与触摸不冲突。

```
ESP32-S3                   传感器组（与触摸屏共线）
┌──────────┐              ┌──────────────────────────────┐
│  GPIO 13 ├──── SCL ────►│ CST3530   (0x58) SCL (触摸)  │
│          │              │ AHT20     (0x38) SCL (温湿度)│
│          │              │ BMP280    (0x76) SCL (气压)  │
│          │              │ MPU9250   (0x68) SCL (IMU)   │
│          │              │ HMC5883L  (0x1E) SCL (罗盘)  │
│  GPIO 20 ├──── SDA ────►│ CST3530   (0x58) SDA         │
│          │              │ AHT20     (0x38) SDA         │
│          │              │ BMP280    (0x76) SDA         │
│          │              │ MPU9250   (0x68) SDA         │
│          │              │ HMC5883L  (0x1E) SDA         │
│  GPIO  8 ├──── INT ────►│ CST3530 触摸中断             │
│  GPIO  3 ├──── AN ─────►│ GUVA-S12SD (UV) 模拟输出     │
│      3V3 ├──── VCC ────►│ 所有传感器 VCC (3.3V)        │
│      GND ├──── GND ────►│ 所有传感器 GND               │
└──────────┘              └──────────────────────────────┘
    I2C 频率：100kHz（降低以保证上拉较弱时稳定通信）
```

**AHT20 温湿度模块**

```
ESP32-S3                   AHT20 (外接模块)
┌──────────┐              ┌─────────────┐
│  GPIO 13 ├──── SCL ────►│ SCL         │
│  GPIO 20 ├──── SDA ────►│ SDA         │
│      3V3 ├──── VCC ────►│ VCC (3.3V)  │
│      GND ├──── GND ────►│ GND         │
└──────────┘              └─────────────┘
                           I2C 地址: 0x38 (固定)
```

**BMP280 气压计模块**

```
ESP32-S3                   BMP280 (外接模块)
┌──────────┐              ┌─────────────┐
│  GPIO 13 ├──── SCL ────►│ SCL         │
│  GPIO 20 ├──── SDA ────►│ SDA         │
│      3V3 ├──── VCC ────►│ VCC (3.3V)  │
│      GND ├──── GND ────►│ GND         │
└──────────┘              └─────────────┘
                           I2C 地址: 0x76 (SDO=GND) 或 0x77 (SDO=VCC)
```

**MPU9250/6500 九轴 IMU 模块**

```
ESP32-S3                   MPU9250/6500 (外接模块)
┌──────────┐              ┌─────────────┐
│  GPIO 13 ├──── SCL ────►│ SCL         │
│  GPIO 20 ├──── SDA ────►│ SDA         │
│      3V3 ├──── VCC ────►│ VCC (3.3V)  │
│      GND ├──── GND ────►│ GND         │
│      —   │              │ AUX/AK8963  │
│      —   │              │  (内置磁力计，自动 bypass)
└──────────┘              └─────────────┘
                           I2C 地址: 0x68 (AD0=GND) 或 0x69 (AD0=VCC)
                           内置 AK8963 磁力计 @0x0C (自动启用 bypass)
```

**HMC5883L / QMC5883L 三轴磁力计模块（独立罗盘）**

> ℹ️ **商品识别提示**：淘宝「HMC5883L」模块绝大多数实为 **QMC5883L**
> （中国兼容芯片），区别：器件地址 **0x2C**（非 0x1E）、数据 X-Y-Z 小端序、
> 寄存器布局不同。本项目同时集成两个驱动，`sensor_manager` 自动识别。

```
ESP32-S3                   HMC5883L / QMC5883L (外接模块)
┌──────────┐              ┌─────────────────────────┐
│  GPIO 13 ├──── SCL ────►│ SCL                     │
│  GPIO 20 ├──── SDA ────►│ SDA                     │
│      3V3 ├──── VCC ────►│ VCC (3.3V)              │
│      GND ├──── GND ────►│ GND                     │
│      —   │              │ DRDY (未接)             │
│      —   │              │ S1,S2 (GND)             │
└──────────┘              └─────────────────────────┘
                           HMC5883L: I2C 地址 0x1E (固定)
                           QMC5883L: I2C 地址 0x2C (固定)
```

**磁力计三级优先级策略**：

```
  drv_hmc5883l_is_ready() ──┬──► ✅ 使用 HMC5883L (@0x1E)
                             │
                             └──► drv_qmc5883l_is_ready() ──┬──► ✅ 使用 QMC5883L (@0x2C)
                                                              │
                                                              └──► 使用 AK8963 (@0x0C)
                                                                    （MPU-9250 内置）
```

> **自动识别机制**：
> - I2C 扫描发现 @0x1E → 是 HMC5883L（罕见）
> - I2C 扫描发现 @0x2C → 是 QMC5883L（淘宝/AliExpress 常见）
> - 两个地址都无应答 → 依赖 MPU-9250 内置 AK8963（要求 MPU-9250 含磁力计）
>
> **⚠️ GPIO12/14 禁用警告**：TK021F2699 板子上 GPIO12/14 与 LCD RGB 数据线
> 存在硬件耦合，禁止接入任何 I2C 设备，否则会导致 LCD 黑屏。所有传感器
> 必须并接到 GPIO13(SCL)/GPIO20(SDA)。

**其他外设**

```
ESP32-S3                   外设
┌──────────┐              ┌──────────────────┐
│  GPIO 38 ├──── DATA ───►│ WS2812 RGB LED   │
└──────────┘              └──────────────────┘
```

### 板 B — PathFinder_Tracker (ESP32-S3 N16R8)

```
ESP32-S3 N16R8
┌───────────────┐
│               │    ┌──── AcousticEye 模块 ────────────────────┐
│               │    │                                          │
│  GPIO 38 ─────┼────┼──── SDA ────► ES7210 (I2C 0x40)         │
│  GPIO 39 ─────┼────┼──── SCL ────► ES7210                     │
│  GPIO 42 ─────┼────┼──── MCLK ───► ES7210                     │
│  GPIO 41 ─────┼────┼──── BCLK ───► ES7210                     │
│  GPIO 40 ─────┼────┼──── WS ─────► ES7210                     │
│  GPIO 21 ─────┼────┼──── DOUT ───► ES7210 (I2S DIN)           │
│  GPIO 45 ─────┼────┼──── PA_EN ──► 板级音频使能 (HIGH=开启)    │
│               │    │                                          │
│               │    └──────────────────────────────────────────┘
│               │
│               │    ┌──── OV2640 摄像头 (DVP 接口) ────────────┐
│               │    │                                          │
│  GPIO  4 ─────┼────┼──── SIOD (SDA)  SCCB I2C                │
│  GPIO  5 ─────┼────┼──── SIOC (SCL)  SCCB I2C                │
│  GPIO  6 ─────┼────┼──── VSYNC                                │
│  GPIO  7 ─────┼────┼──── HREF                                 │
│  GPIO  8 ─────┼────┼──── D2                                   │
│  GPIO  9 ─────┼────┼──── D1                                   │
│  GPIO 10 ─────┼────┼──── D3                                   │
│  GPIO 11 ─────┼────┼──── D0                                   │
│  GPIO 12 ─────┼────┼──── D4                                   │
│  GPIO 13 ─────┼────┼──── PCLK                                 │
│  GPIO 15 ─────┼────┼──── XCLK (16MHz 输出)                    │
│  GPIO 16 ─────┼────┼──── D7                                   │
│  GPIO 17 ─────┼────┼──── D6                                   │
│  GPIO 18 ─────┼────┼──── D5                                   │
│               │    │                                          │
│               │    │    ┌──── 电源与控制（非 GPIO） ────────┐  │
│               │    │    │ VCC   ── 3.3V（~100mA，禁接 5V）  │  │
│               │    │    │ GND   ── GND（共地）              │  │
│               │    │    │ RESET ── 3.3V 或悬空（低有效）    │  │
│               │    │    │ PWDN  ── GND 或悬空（高=休眠）   │  │
│               │    │    └──────────────────────────────────┘  │
│               │    └──────────────────────────────────────────┘
│               │
│               │    ┌──── MG90S 舵机 ──────────────────────────┐
│               │    │                                          │
│  GPIO 14 ─────┼────┼──── 橙色信号线 ──► Pan (水平轴)          │
│  GPIO 47 ─────┼────┼──── 橙色信号线 ──► Tilt (垂直轴)         │
│               │    │    红线 ──────────► VCC (5V)              │
│               │    │    棕线 ──────────► GND                   │
│               │    │                                          │
│               │    └──────────────────────────────────────────┘
│               │
│               │    ┌──── WS2812 灯环 (36 LED) ───────────────┐
│  GPIO 48 ─────┼────┼──── DIN ──────► WS2812 数据输入          │
│               │    │                                          │
│               │    └──────────────────────────────────────────┘
│               │
│               │    ┌──── MAX98357A 功放 (I2S1 TTS) ──────────┐
│  GPIO  1 ─────┼────┼──── BCLK (位时钟)                        │
│  GPIO  2 ─────┼────┼──── WS   (字选择)                        │
│  GPIO  3 ─────┼────┼──── DIN  (数据输入)                       │
│               │    │    VIN ──────────► 5V                     │
│               │    │    GND ──────────► 共地                   │
│               │    │                                          │
│               │    └──────────────────────────────────────────┘
│               │
│               │    ┌──── UART 板间通信 (→ 板 A) ─────────────┐
│  GPIO 19 ─────┼────┼──── TX ────────► 板 A RX                │
│  GPIO 20 ─────┼────┼──── RX ────────► 板 A TX                │
│               │    │    115200 bps                             │
│               │    └──────────────────────────────────────────┘
└───────────────┘
```

> **注意**：GPIO 26-37 被 Octal PSRAM 占用，不可复用。

> **OV2640 PWDN/RESET 接线说明**：`PWDN` 与 `RESET` 均**不占用 GPIO**，
> 在软件层 `CAM_PIN_PWDN = -1`、`CAM_PIN_RESET = -1`（见
> `tracker_config.h`），硬件层直接将 `PWDN` 接 GND（常驻工作模式，高=休眠/低=工作），
> `RESET` 接 3.3V 或悬空（低有效，正常工作需保持高电平）。
>
> **设计原因**：ESP32-S3-N16R8 可用 GPIO 已被 DVP 数据线（D0–D7）、VSYNC/HREF/PCLK/XCLK、
> SCCB I2C、ES7210 I2S/I2C、WS2812、舵机 PWM 等全部占满。其中 `PCLK` 占用 GPIO13，
> 正是 `PA_EN` 必须从 GPIO13 迁移至 GPIO45 的原因。
>
> **如何启用软件 PWDN 控制**：如需对摄像头进行软件掉电控制（低功耗场景），
> 修改 `PathFinder_Tracker/main/tracker_config.h` 中的 `CAM_PIN_PWDN` 宏为目标 GPIO
> 编号即可，`drv_ov2640.c` 中 `.pin_pwdn = CAM_PIN_PWDN` 会自动生效。
> 可选空闲 GPIO 仅剩 GPIO0（需注意 BOOT 约束）或扩展板新增的引脚。

### I2C 设备地址总览

| 设备 | 地址 | 所属板 | 总线 |
|------|------|--------|------|
| AHT20 | 0x38 | 板 A | I2C-1 |
| BMP280 | 0x76 | 板 A | I2C-1 |
| MPU6050 | 0x68 | 板 A | I2C-1 |
| **HMC5883L** | **0x1E** | **板 A** | **I2C-1** |
| AK8963 (MPU-9250 内置) | 0x0C | 板 A | I2C-1 (bypass) |
| CST3530 | 0x58 | 板 A | I2C-0 |
| ES7210 | 0x40 | 板 B | I2C-1 |
| OV2640 SCCB | 0x30 | 板 B | I2C-0 |

---

## 多模态追踪工作流程

```
                    ┌──────────────────┐
                    │   声源粗定位      │
                    │  ES7210 4ch 采集  │
                    │  GCC-PHAT 算法    │
                    │  输出: 0°~360°    │
                    └────────┬─────────┘
                             │ 角度有效
                             ▼
                    ┌──────────────────┐
                    │  舵机粗调        │
                    │  Pan/Tilt 转向   │
                    │  声源方向        │
                    └────────┬─────────┘
                             │ 云台对准
                             ▼
                    ┌──────────────────┐
                    │  视觉精追踪      │
                    │  OV2640 人脸检测  │
                    │  ESP-DL 推理     │
                    │  坐标反馈微调    │
                    └────────┬─────────┘
                             │ 人脸锁定
                             ▼
                    ┌──────────────────┐
                    │  闭环保持        │
                    │  音频+视觉融合   │
                    │  丢失→搜索→重捕  │
                    └──────────────────┘
```

**状态机**：`IDLE → ACOUSTIC_TRACK → FACE_TRACK → SEARCH → (循环)`

---

## 已实现功能模块

### 板 A — PathFinder_EMOTE 固件

#### Phase 1：传感器 + 动态表情联动 ✅

| 模块 | 文件 | 功能 |
|------|------|------|
| 传感器驱动 | `drivers/drv_aht20.c/h`、`drv_bmp280.c/h`、`drv_mpu9250.c/h`、`drv_hmc5883l.c/h`、`drv_uv_adc.c/h` | AHT20 温湿度、BMP280 气压海拔、MPU6050/9250 IMU、HMC5883L 磁力计、GUVA-S12SD UV |
| 传感器管理 | `sensor_manager.c/h` | 双任务采样（env 1Hz + imu 25Hz），I2C-1 总线（GPIO12/14，400kHz），磁力计优先级 HMC5883L > AK8963 |
| 运动引擎 | `motion_engine.c/h` | 13 种运动事件检测（加减速、转向、颠簸、碰撞等） |
| 表情引擎 | `emote_engine.c/h` | 运动事件→EAF 表情映射（23 种动画），环境异常触发 |
| BLE GATT | `ble_gatt_server.c/h` | C2 环境(1Hz)、C3 运动(25Hz)、C4 表情(on-change) Notify |
| LVGL UI | `main.c`、`LCD.c`、`app_emote_assets.c` | 480×480 圆形屏、EAF 表情居中(330×330)、胶囊数据条 |
| 飞行仪表 | `flight_instruments.c/h` | 姿态指引仪 + 指南针/海拔/气压双页滑动 |
| Wi-Fi 配网 | `wifi_config_manager.c/h`、`web_portal.c/h` | BLE+Web 双模配网、NVS 存储、STA/AP 切换 |
| 配网 UI | `provision_screen.c/h` | 4 状态 LVGL 覆盖层 |

#### Phase 5：Mesh 组网 ✅

| 模块 | 文件 | 功能 |
|------|------|------|
| Mesh 节点 | `mesh_node.c/h` | ESP-WIFI-MESH ROOT 节点（板 A），连接路由器 |
| ESP-NOW | `mesh_espnow.c/h` | ESP-NOW 广播通道 |
| 协议 | `mesh_common/mesh_protocol.c/h` | 紧凑帧格式（MSG_TYPE+SEQ+LEN+PAYLOAD+CRC8） |

### 板 B — PathFinder_Tracker 固件

#### Phase 1-2：声源定位 + 舵机云台 ✅

| 模块 | 文件 | 功能 |
|------|------|------|
| ES7210 驱动 | `drivers/drv_es7210.c/h` | I2C 配置 + I2S TDM 4ch 48kHz 采集 |
| 声源定位 | `audio/sound_localizer.c/h` | GCC-PHAT 算法，4 通道→角度输出 |
| 舵机驱动 | `drivers/drv_servo.c/h` | 2×MG90S PWM 50Hz，Pan(0-180°) + Tilt(0-180°) |
| WS2812 | `drivers/drv_ws2812.c/h` | 36 LED 灯环，声源方向可视化 |
| UART 通信 | `drivers/drv_uart_comm.c/h` | 板间通信 115200bps |
| 状态机 | `tracker_state_machine.c/h` | IDLE→ACOUSTIC_TRACK→FACE_TRACK→SEARCH 闭环 |

#### Phase 3：视觉追踪 + Web 预览 ✅

| 模块 | 文件 | 功能 |
|------|------|------|
| OV2640 驱动 | `vision/drv_ov2640.c/h` | DVP 8-bit 并行接口，SCCB I2C 配置 |
| 人脸检测 | `vision/face_detector.cpp/h` | ESP-DL 推理，Core-1 独立任务 |
| 人脸追踪 | `vision/face_tracker.c/h` | 人脸坐标→舵机微调闭环 |
| 视觉任务 | `vision/vision_task.c/h` | Core-1 capture→detect→publish，overwrite queue |
| Web 预览 | `vision/web_viewer.c/h` | Wi-Fi AP + HTTP 摄像头实时预览 |

#### Phase 5：Mesh 组网 ✅

| 模块 | 文件 | 功能 |
|------|------|------|
| Mesh 节点 | `comm/mesh_node.c/h` | ESP-WIFI-MESH CHILD 节点（板 B） |
| ESP-NOW | `comm/mesh_espnow.c/h` | ESP-NOW 广播 |
| 通信链路 | `comm/comm_link.c/h` | 心跳、数据上报 |
| 协议 | `comm/mesh_protocol.c/h` | 共享消息协议（与板 A 一致） |

### Flutter App — PathFinder_Dashboard

| 层 | 目录 | 核心内容 |
|----|------|----------|
| BLE 通信 | `core/ble/` | ReactiveBleService、BLE Codec（对齐 ESP32 C 结构体）、UUID 常量 |
| 数据持久 | `core/storage/` | Drift SQLite（Env/Motion/Emote 三表）、DAO、CSV 导出 |
| 环境页 | `features/environment/` | 5 Metric Card + fl_chart 折线图 |
| 运动页 | `features/motion/` | 姿态指示器 + IMU 波形 + 事件时间轴 |
| 表情页 | `features/emote/` | 23 个 EAF 动画展示 + 映射表 |
| 连接面板 | `features/connection/` | 底部弹窗扫描/连接/状态指示 |
| 状态管理 | `shared/providers/` | Riverpod（sensor_provider + ble_provider） |
| 主题 | `app/theme/` | Racing Dark 赛车暗色 + EMA 胶囊风格 |

---

## 技术栈

### ESP32 固件端

| 层 | 技术 | 版本 | 用途 |
|----|------|------|------|
| SDK | ESP-IDF | v6.0+ | 官方开发框架 |
| GUI | LVGL | 8.3.11 | 嵌入式图形库 |
| 动画 | EAF | ESP-IDF component | 表情动画播放器 |
| AI | ESP-DL | espressif 组件 | 人脸检测推理 |
| 存储 | NVS | ESP-IDF native | 非易失性存储 |
| BLE | NimBLE | ESP-IDF native | 低功耗蓝牙 |
| Mesh | ESP-WIFI-MESH | ESP-IDF native | 板间组网 |
| ADC | esp_adc | ESP-IDF v6.0 | ADC 校准 API |

### Flutter App 端

| 层 | 库 | 版本 | 用途 |
|----|----|------|------|
| 框架 | Flutter | >=3.8.0 | Android 应用框架 |
| 状态管理 | flutter_riverpod | ^2.6.1 | 编译时安全流式数据 |
| BLE | flutter_reactive_ble | ^5.3.1 | 纯 Dart BLE 通信 |
| 图表 | fl_chart | ^0.70.2 | 折线图/波形图 |
| 数据库 | drift | ^2.22.1 | 类型安全 SQL ORM |
| 导航 | go_router | ^14.8.1 | 声明式路由 |
| 权限 | permission_handler | ^11.3.1 | Android 运行时权限 |

---

## BLE 通信协议

### GATT Service 结构

```
Service UUID: 0000fe00-0000-1000-8000-00805f9b34fb
├── C2 Environment Data   (NOTIFY @1Hz)        UUID: 0000fe02-...
├── C3 Motion Data        (NOTIFY @25Hz)       UUID: 0000fe03-...
├── C4 Emote State        (NOTIFY @on-change)  UUID: 0000fe04-...
```

### 数据帧格式

**C2 环境数据帧（16 字节）**

| Offset | Field | Type | Unit | 说明 |
|--------|-------|------|------|------|
| 0 | temperature | int16 | 0.01°C | 温度 |
| 2 | humidity | uint16 | 0.01% | 湿度 |
| 4 | pressure | uint32 | Pa | 气压 |
| 8 | altitude | int32 | m | 海拔 |
| 12 | uv_index | uint16 | 0.01 | UV 指数 |
| 14 | reserved | uint16 | - | 保留 |

**C3 运动数据帧（44 字节）**

| Offset | Field | Type | Unit | 说明 |
|--------|-------|------|------|------|
| 0 | accel[3] | float[3] | g | 加速度 X/Y/Z |
| 12 | gyro[3] | float[3] | °/s | 陀螺仪 X/Y/Z |
| 24 | roll | float | ° | Roll 角 |
| 28 | pitch | float | ° | Pitch 角 |
| 32 | yaw | float | ° | Yaw 角 |
| 36 | event_id | uint8 | - | 运动事件 ID（0~12） |
| 37 | reserved | uint8[7] | - | 保留 |

**C4 表情状态帧（8 字节）**

| Offset | Field | Type | 说明 |
|--------|-------|------|------|
| 0 | emote_id | uint8 | 表情动画 ID（0~22） |
| 1 | name_len | uint8 | 表情名称长度 |
| 2-7 | name | char[6] | 表情名称 |

### Mesh 板间消息协议

```
帧格式: [MSG_TYPE(1)][SEQ(1)][PAYLOAD_LEN(1)][PAYLOAD(0~246)][CRC8(1)]
最大帧长: 250 字节（兼容 ESP-NOW 限制）

消息类型:
  0x01  MSG_ANGLE_DATA     声源角度 (B→A)
  0x02  MSG_TRACK_STATE    追踪状态 (B→A)
  0x03  MSG_FACE_INFO      人脸信息 (B→A)
  0x04  MSG_SERVO_CTRL     舵机控制 (A→B)
  0x05  MSG_MODE_SWITCH    模式切换 (A→B)
  0x10  MSG_HEARTBEAT      心跳 (B→A, 500ms)
```

### 运动事件映射表

| ID | 事件 | 触发条件 | 表情 |
|----|------|----------|------|
| 0 | 急加速 | Accel > 0.3g | confident_08 |
| 1 | 急减速 | Accel < -0.3g | shocked_05s |
| 2 | 正常加速 | 0.1g < Accel < 0.3g | smile_05s |
| 3 | 正常减速 | -0.1g > Accel > -0.3g | sigh_20s |
| 4 | 左转 | Gyro_Z > 30°/s | shy_20s |
| 5 | 右转 | Gyro_Z < -30°/s | shy_20s |
| 6 | 急左转 | Gyro_Z > 60°/s | panic_05s |
| 7 | 急右转 | Gyro_Z < -60°/s | panic_05s |
| 8 | 上坡 | Pitch > 10° | leisure_05s |
| 9 | 下坡 | Pitch < -10° | investigate |
| 10 | 颠簸 | Accel variance > 0.2g² | mock_05s |
| 11 | 碰撞 | Accel peak > 2g | angry_20s |
| 12 | 静止 | Motion < threshold | asleep_215s |

### 表情动画列表（23 个）

| ID | 文件名 | 中文名 | | ID | 文件名 | 中文名 |
|----|--------|--------|-|----|--------|--------|
| 0 | angry_20s | 生气 | | 12 | ponder_05s | 沉思 |
| 1 | asleep_215s | 睡着 | | 13 | question_05s | 疑问 |
| 2 | badminton_12 | 打羽毛球 | | 14 | sad_05s15s | 悲伤 |
| 3 | confident_08 | 自信 | | 15 | shocked_05s | 震惊 |
| 4 | cry_10s_20s | 哭泣 | | 16 | shy_20s | 害羞 |
| 5 | investigate | 专注调查 | | 17 | sigh_20s | 叹气 |
| 6 | laugh_05s_10 | 大笑 | | 18 | smile_05s | 微笑 |
| 7 | leisure_05s | 休闲 | | 19 | smile_static | 微笑静止 |
| 8 | mock_05s | 嘲讽 | | 20 | snigger_10s | 窃笑 |
| 9 | music_25s | 听音乐 | | 21 | yawn_20s | 打哈欠 |
| 10 | mute_05s | 无语 | | 22 | yummy_20_s | 享受 |
| 11 | panic_05s_15 | 恐慌 | | | | |

---

## 硬件平台规格

### 板 A — TK021F2699

| 参数 | 规格 |
|------|------|
| 主控 | ESP32-S3-WROOM-1（16MB Flash, 8MB Octal PSRAM 80MHz） |
| LCD | ST7701S RGB565 圆形屏 480×480 |
| 触摸 | CST3530 电容触摸（I2C 0x58） |
| 传感器 | AHT20 + BMP280 一体模块、GY-521 MPU6050、HMC5883L 磁力计、GUVA-S12SD UV |
| LED | WS2812 RGB 灯环 |

### 板 B — ESP32-S3 N16R8

| 参数 | 规格 |
|------|------|
| 主控 | ESP32-S3-WROOM-1（16MB Flash, 8MB Octal PSRAM） |
| 麦克风 | ES7210 4 通道 MEMS 麦克风阵列（I2C 0x40, I2S TDM 48kHz） |
| 摄像头 | OV2640 DVP 8-bit（SCCB I2C, XCLK 16MHz） |
| 舵机 | 2× MG90S（Pan 水平 + Tilt 垂直，PWM 50Hz） |
| 灯环 | WS2812 × 36 LED（RMT 驱动） |
| 功放 | MAX98357A（I2S1 TTS 语音输出） |
| AI | xiaozhi-esp32 语音助手（预留） |

---

## 文件结构

```
PathFinder_LCD/
├── PathFinder_EMOTE/              # 板 A 固件
│   ├── main/
│   │   ├── main.c                 # 主程序（LCD + 传感器 + BLE + Mesh）
│   │   ├── LCD.c/h                # ST7701S RGB LCD 驱动
│   │   ├── app_emote_assets.c/h   # EAF 表情资源管理
│   │   ├── sensor_manager.c/h     # 传感器管理器
│   │   ├── motion_engine.c/h      # 运动分析引擎
│   │   ├── emote_engine.c/h       # 表情联动引擎
│   │   ├── ble_gatt_server.c/h    # BLE GATT Server
│   │   ├── flight_instruments.c/h # 飞行仪表盘
│   │   ├── wifi_config_manager.c/h # Wi-Fi 配网管理
│   │   ├── web_portal.c/h         # Captive Portal Web 配网
│   │   ├── provision_screen.c/h   # LVGL 配网 UI
│   │   ├── mesh_espnow.c/h        # ESP-NOW 通信
│   │   ├── mesh_node.c/h          # Mesh 节点管理
│   │   ├── mesh_common/           # 板 A/B 共享协议
│   │   │   └── mesh_protocol.c/h
│   │   ├── drivers/               # 传感器驱动
│   │   │   ├── drv_aht20.c/h
│   │   │   ├── drv_bmp280.c/h
│   │   │   ├── drv_mpu9250.c/h
│   │   │   ├── drv_hmc5883l.c/h
│   │   │   └── drv_uv_adc.c/h
│   │   ├── CMakeLists.txt
│   │   └── idf_component.yml
│   ├── managed_components/        # ESP-IDF 组件
│   ├── partitions.csv             # Flash 分区表
│   ├── emote-assets.bin           # EAF 表情资源
│   └── CMakeLists.txt
│
├── PathFinder_Tracker/            # 板 B 固件
│   ├── main/
│   │   ├── main.c                 # 主程序（音频+视觉+舵机+Mesh）
│   │   ├── tracker_config.h       # 全局引脚与参数配置
│   │   ├── tracker_state_machine.c/h # 追踪状态机
│   │   ├── drivers/               # 外设驱动
│   │   │   ├── drv_es7210.c/h
│   │   │   ├── drv_servo.c/h
│   │   │   ├── drv_ws2812.c/h
│   │   │   └── drv_uart_comm.c/h
│   │   ├── audio/                 # 声源定位
│   │   │   └── sound_localizer.c/h
│   │   ├── vision/                # 视觉追踪
│   │   │   ├── drv_ov2640.c/h
│   │   │   ├── face_detector.cpp/h
│   │   │   ├── face_tracker.c/h
│   │   │   ├── vision_task.c/h
│   │   │   └── web_viewer.c/h
│   │   ├── comm/                  # 板间通信
│   │   │   ├── mesh_node.c/h
│   │   │   ├── mesh_espnow.c/h
│   │   │   ├── mesh_protocol.c/h
│   │   │   └── comm_link.c/h
│   │   ├── CMakeLists.txt
│   │   └── idf_component.yml
│   ├── managed_components/        # ESP-IDF 组件（含 ESP-DL、esp32-camera）
│   ├── partitions.csv
│   └── CMakeLists.txt
│
├── PathFinder_Dashboard/          # Flutter App
│   ├── lib/
│   │   ├── main.dart
│   │   ├── app/                   # 主题系统
│   │   ├── core/                  # BLE 通信 + 数据持久层
│   │   ├── features/              # 功能模块（4 个 Screen + Connection）
│   │   └── shared/                # 模型/Provider/组件
│   ├── assets/emotes/             # 23 个 EAF 动画
│   ├── test/                      # 单元测试
│   └── pubspec.yaml
│
├── AcousticEye/AcousticEye/       # AcousticEye 独立声源定位固件
│   ├── main/
│   │   ├── main.c                 # 声源定位 + WS2812 方向显示
│   │   ├── ws2812.c/h             # LED 灯环驱动
│   │   └── calc/                  # GCC-PHAT 声源定位算法
│   ├── components/
│   │   ├── es7210/                # ES7210 麦克风阵列驱动
│   │   ├── calc/                  # 方向计算
│   │   └── esp_dsp/               # DSP 库
│   └── CMakeLists.txt
│
└── docs/superpowers/              # 设计文档
    ├── specs/                     # 设计规格
    └── plans/                     # 实施计划
```

---

## 开发环境与构建

### ESP32 固件构建

```bash
# 安装 ESP-IDF v6.0+
git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3 && source export.sh

# 构建板 A (EMOTE)
cd PathFinder_EMOTE
idf.py build
idf.py -p /dev/cu.usbmodem5AF61192361 flash
idf.py -p /dev/cu.usbmodem5AF61192361 monitor

# 烧录 EAF 表情资源
esptool.py --port /dev/cu.usbmodem5AF61192361 write_flash 0x410000 emote-assets.bin

# 构建板 B (Tracker)
cd PathFinder_Tracker
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

### Flutter App 构建

```bash
cd PathFinder_Dashboard
flutter pub get
dart run build_runner build     # 生成 Drift 代码
flutter run                     # 开发模式
flutter build apk --release     # 发布 APK
```

---

## 关键修复经验

| 问题 | 根因 | 修复 |
|------|------|------|
| LCD 黑屏 + 串口无日志 | USB-CDC 占用 GPIO20，与 LCD SPI 冲突 | 切换 `CONFIG_ESP_CONSOLE_UART_DEFAULT`，释放 GPIO20 |
| Flutter 扫不到 BLE 设备 | ESP32 使用 128-bit UUID，Flutter 扫描 16-bit | NimBLE 改用 `BLE_UUID16_INIT` |
| UI 卡 loading | Broadcast stream 不缓存初始值 | async* getter + _currentState 缓存 |
| MPU6050 接入后花屏 | 高频采样 + 单帧缓冲渲染竞态 | 启用双帧缓冲 `CONFIG_LCD_RGB_BUFFER_NUM=2` |
| UV 传感器恒为 0 | ADC GPIO4 被 LCD 占用 | 迁移至 GPIO3/ADC1_CH2 |
| ES7210 启动挂死 | 供电 5V / 未共地 / I2C 接线错误 | 3.3V 供电 + 共地 + SDA=38/SCL=39 |
| PA_EN 无输出 | GPIO13 被 OV2640 PCLK 占用 | PA_EN 迁移至 GPIO45 |

---

## 后续规划

| 阶段 | 功能 | 状态 |
|------|------|------|
| Phase 4 | xiaozhi AI 语音助手集成（MAX98357A TTS 输出） | 预留 |
| Phase 5 | ESP-WIFI-MESH + ESP-NOW 板间组网 | ✅ 已实现 |
| Phase 6 | 云端数据同步、多设备协同 | 待规划 |
| Phase 7 | OTA 固件升级 | 待规划 |

---

## 许可证

本项目遵循 LICENSE 文件规定的开源协议。
