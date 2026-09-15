# PathFinder_EMOTE — 传感器集成与 UI 文档

## 1. 传感器概述

### 1.1 Sharp GP2Y1010AU0F (DFRobot SEN0144)

| 参数 | 规格 |
|------|------|
| **型号** | Sharp GP2Y1010AU0F |
| **DFRobot SKU** | SEN0144 |
| **类型** | 红外光学粉尘传感器 |
| **检测原理** | 红外 LED 脉冲照射 → 光电晶体管检测粉尘散射反射光 → 模拟电压输出 |
| **敏感颗粒** | 室内粉尘 (PM2.5 量级) |
| **灵敏度** | 0.5V / 0.1mg/m³ |
| **工作电压** | 5–7V DC (典型 5V) |
| **工作电流** | 11mA 典型, 20mA 最大 (LED 脉冲期间) |
| **输出信号** | 模拟电压 (Vo), 0–3.5V |
| **检测范围** | 0.1–0.5 mg/m³ (最佳线性区) |
| **响应时间** | <10ms (单个脉冲周期) |
| **工作温度** | -10°C ~ +65°C |
| **裸传感器尺寸** | 46mm × 30mm × 17.6mm (Sharp 原厂) |
| **含转接板尺寸** | 59mm × 45mm × 22mm (DFRobot Wiki) |
| **重量** | ~15g (裸传感器) |

#### 裸传感器 6-pin 定义

| Pin | 名称 | 说明 |
|-----|------|------|
| 1 | LED-IN | LED 脉冲驱动输入 (需 150Ω 限流电阻到 5V) |
| 2 | LED-GND | LED 地 (需 220µF 电容连接到 Pin 3) |
| 3 | LED-GND | LED 地 |
| 4 | S-GND | 信号地 |
| 5 | Vo | 模拟电压输出 |
| 6 | Vcc | 供电 (5–7V) |

> **注意**：裸传感器需要外接 150Ω 电阻 (Pin 1↔5V) 和 220µF 电容 (Pin 2↔Pin 3) 才能工作。

---

### 1.2 DFRobot Gravity Dust Sensor Adapter

DFRobot 提供专用的转接板，将裸传感器简化为 Gravity 4 线接口：

| 转接板特性 | 说明 |
|-----------|------|
| **内置元件** | 150Ω 限流电阻 + 220µF 滤波电容 |
| **接口** | DFRobot Gravity 标准 4-pin (VCC / GND / A / D) |
| **供电** | 5V |
| **A (模拟)** | 模拟电压输出 (Vo) |
| **D (数字)** | LED 脉冲控制 (数字输入) |

#### 转接板引脚定义

| 转接板引脚 | 颜色 | 说明 |
|-----------|------|------|
| **VCC** | 红 | 5V 供电 |
| **GND** | 黑 | 地 |
| **D** | 蓝 | LED 脉冲控制 (数字输入) |
| **A** | 黄 | 模拟电压输出 (Vo) |

> 适配器内部已集成 150Ω 电阻和 220µF 电容，用户无需额外焊接元器件。
>
> **DFRobot 官方资料：**
> - 产品页 (中文): https://www.dfrobot.com.cn/goods-827.html
> - 产品页 (英文): https://www.dfrobot.com/product-1063.html
> - Wiki 技术文档: https://wiki.dfrobot.com/sen0144/
> - Arduino 示例代码: https://wiki.dfrobot.com/sen0144/docs/18418
> - 原理图 PDF: https://www.dfrobot.com.cn/images/upload/File/20131204142145xa068u.pdf
> - Sharp 应用笔记: https://global.sharp/products/device/lineup/data/pdf/datasheet/gp2y1010au_appl_e.pdf

---

## 2. 硬件接线

### 2.1 A板 (PathFinder_EMOTE / TK021F2699) 引脚分配

A板使用 ESP32-S3-WROOM-1-N16R8，GPIO 资源已被 LCD、I2C 传感器等大量占用。经分析，**GPIO1 (ADC1_CH0)** 是唯一可用的 ADC1 引脚（GPIO3 和 GPIO8 已被 UV 传感器占用），**GPIO39** 可用作 LED 脉冲控制。

#### 关键约束

- **必须使用 ADC1**：WiFi 开启时 ADC2 不可用，A板运行 Wi-Fi/Mesh 必须用 ADC1
- **GPIO3 和 GPIO8 均已被 UV 传感器占用**，粉尘传感器改用 GPIO1
- **GPIO39** 空闲且支持输出，适合 LED 脉冲控制
- 3.3V GPIO 输出足够关断 5V LED（传感器 LED 控制为低电平有效）

### 2.2 接线图

```
  Sharp GP2Y1010AU0F (DFRobot SEN0144)
  ┌─────────────────────────┐
  │    ┌─────────────────┐  │
  │    │   红外LED窗口    │  │
  │    │   ⊙   ⊙        │  │
  │    └─────────────────┘  │
  │                         │
  │  DFRobot Gravity        │
  │  Adapter Board          │
  │  ┌───────┐              │
  │  │ 150Ω  │              │
  │  │ 220µF │              │
  │  └───────┘              │
  │                         │
  │  VCC  GND   D    A      │
  │   │    │    │    │      │
  └───┼────┼────┼────┼──────┘
      │    │    │    │
      │    │    │    │
  ════╪════╪════╪════╪═════════════════════
      │    │    │    │
  ┌───┼────┼────┼────┼──────────────────┐
  │   │    │    │    │                  │
  │   ▼    ▼    ▼    ▼                  │
  │  5V   GND  GPIO39  GPIO1            │
  │                   (ADC1_CH0)        │
  │                                     │
  │     ESP32-S3 (TK021F2699 A板)       │
  └─────────────────────────────────────┘

  接线表:
  ┌─────────────────┬───────────────────┬──────────────┐
  │ 适配器引脚       │ A板连接           │ 说明          │
  ├─────────────────┼───────────────────┼──────────────┤
  │ VCC (红)        │ 5V                │ 供电          │
  │ GND (黑)        │ GND               │ 公共地        │
  │ D   (蓝)        │ GPIO39            │ LED脉冲控制   │
  │ A   (黄)        │ GPIO1 (ADC1_CH0)  │ 模拟电压输出  │
  └─────────────────┴───────────────────┴──────────────┘
```

### 2.3 LED 脉冲采样时序

传感器要求精确的微秒级脉冲控制（总周期 10ms）：

```
          ┌──────────────────────────────────────────┐
          │              一个采样周期 (10ms)            │
          │                                          │
  LED ────┘     ┌──────────────────────────┐
  (HIGH)        │                          │
         280µs  │       40µs         9680µs │
         等待   │  采样  等待   脉冲间隔     │
                │                          │
          LED=LOW                      LED=HIGH
          (点亮IR LED)                  (熄灭IR LED)
                ▲
                │
            ADC 读取
            (此时点)
```

| 阶段 | 时长 | 操作 |
|------|------|------|
| 1. LED 点亮 | — | `gpio_set_level(LED, 0)` |
| 2. 采样等待 | 280µs | `esp_rom_delay_us(280)` |
| 3. **ADC 读取** | — | `adc_oneshot_read()` |
| 4. 延时 | 40µs | `esp_rom_delay_us(40)` |
| 5. LED 熄灭 | — | `gpio_set_level(LED, 1)` |
| 6. 脉冲间隔 | 9680µs | `esp_rom_delay_us(9680)` |

每次完整读取执行 **10 次过采样**取平均，总耗时 ~100ms。

---

## 3. 浓度换算与 AQI 等级

### 3.1 Chris Nafis 线性公式

```
density (mg/m³) = 0.17 × Vo − 0.1
(负值裁剪到 0)
```

参考: http://www.howmuchsnow.com/arduino/airquality/ (Chris Nafis (c) 2012)

> 此公式与 DFRobot 官方 Wiki SEN0144 示例代码中使用的公式完全一致。
>
> **变更说明**：原始实现中设有 0.6V 阈值门控（Vo < 0.6V 时直接返回 0），
> 但在干净空气中传感器输出通常为 0.3~0.5V，导致浓度始终为 0。
> 现已移除阈值门控，始终计算浓度并裁剪负值到 0。

### 3.2 AQI 五级映射

| 等级 | density_mgm3 范围 | 含义 | 表情联动 |
|------|-------------------|------|---------|
| 0 | < 0.05 mg/m³ | 优 | 正常 |
| 1 | 0.05 – 0.10 mg/m³ | 良 | 正常 |
| 2 | 0.10 – 0.15 mg/m³ | 轻度污染 | 正常 |
| 3 | 0.15 – 0.25 mg/m³ | 中度污染 | sigh (叹气) |
| 4 | ≥ 0.25 mg/m³ | 重度污染 | sad (悲伤) |

---

## 4. 软件集成

### 4.1 驱动文件

| 文件 | 说明 |
|------|------|
| `main/drivers/drv_dust_gp2y.h` | 数据结构 `dust_data_t` + API 声明 |
| `main/drivers/drv_dust_gp2y.c` | LED 脉冲采样、ADC 过采样、浓度换算、AQI 映射 |

#### 核心数据结构

```c
typedef struct {
    uint16_t raw;           // ADC 原始值 (过采样后)
    float    voltage;       // 校准电压 V
    float    density_mgm3;  // 粉尘浓度 mg/m³
    uint8_t  aqi_level;     // 空气质量等级 0~4 (优/良/轻度/中度/重度)
} dust_data_t;
```

#### API

```c
// 初始化 (共享 ADC1 handle，避免与 UV 驱动冲突)
esp_err_t drv_dust_init(adc_oneshot_unit_handle_t adc_handle,
                        adc_channel_t adc_ch, gpio_num_t led_gpio);

// 读取数据 (10 次脉冲过采样, ~100ms)
esp_err_t drv_dust_read(dust_data_t *out);
```

> **重要**：ESP-IDF 中每个 ADC 单元只能创建一个 oneshot handle。
> UV 传感器 (`drv_uv_adc`) 先创建 ADC1 handle，粉尘传感器通过
> `drv_uv_get_adc_handle()` 获取共享 handle，避免 `ESP_ERR_INVALID_STATE`。

### 4.2 传感器管理器集成

`sensor_manager.h` — `env_snapshot_t` 新增字段：

```c
#include "drv_dust_gp2y.h"

typedef struct {
    /* ... 原有环境数据 ... */
    dust_data_t   dust;     // 粉尘浓度
} env_snapshot_t;
```

`sensor_manager.c` — 初始化与采样配置：

```c
#define DUST_ADC_CHANNEL  ADC_CHANNEL_0   /* GPIO1 = ADC1_CH0 */
#define DUST_LED_GPIO     GPIO_NUM_39

// 初始化 (UV 驱动先创建 ADC1 handle，粉尘驱动共享)
drv_uv_init(UV_ADC_UNIT, UV_ADC_CHANNEL);
drv_dust_init(drv_uv_get_adc_handle(), DUST_ADC_CHANNEL, DUST_LED_GPIO);

// env_task 中 1Hz 读取
drv_dust_read(&snap.dust);
```

### 4.3 UI 显示 (环境明细页 DETAIL_ENV)

粉尘数据在环境明细页 **第 7 行** 显示，同时显示电压值用于诊断：

```
┌─────────────────────────┐
│    环境明细 (DETAIL_ENV)   │
│                         │
│  Temp    26.5°C        │
│  Humi    45.2%         │
│  Pres    101325 Pa     │
│  Alt     128.5 m       │
│  UV      3.2           │
│  Light   512           │
│  Dust    0.08  0.85V   │  ← 浓度 + 电压
│                         │
└─────────────────────────┘
```

#### 环境明细页字体规范

| 元素 | 字体 | 颜色 |
|------|------|------|
| 标题 (ENVIRONMENT) | `montserrat_24` | 蓝色 |
| 左侧标签 (Temp, Humi 等) | `montserrat_18` | 纯白 |
| 右侧数值 | `montserrat_24` | 纯白 |

> 电压诊断值可帮助判断传感器状态：
> - 0.8~1.5V → 传感器正常
> - 0.0~0.1V → A 引脚未接好
> - 3.1~3.3V → 引脚悬空或读到 VCC (raw=4095)

### 4.4 异常告警

```c
#define ALERT_DUST_WARNING  0.15f   // 中度污染阈值

// 当粉尘浓度 ≥ 0.15 mg/m³ 时触发告警
```

### 4.5 飞行仪表盘校准弹窗

#### 交互流程

```
表情页 (EAF)
  │ 短按
  ▼
飞行仪表盘 (flight_instruments)
  │ 长按
  ▼
校准确认对话框 (msgbox)
  │ 点击 Start
  ▼
校准进度环 (overlay)
  │ Done / Failed / Timeout
  ▼
自动关闭 → 返回飞行仪表盘
  │ 短按
  ▼
表情页 (EAF)
```

#### 相关文件

| 文件 | 说明 |
|------|------|
| `main/flight_instruments.c` | 仪表盘 UI + 校准弹窗状态机 |
| `main/motion_engine.c` | IMU 校准算法 (采集静止 bias + NVS 持久化) |
| `main/motion_engine.h` | 校准状态枚举与 API |

#### 校准状态机

```c
typedef enum {
    MOTION_CALIB_IDLE,     // 空闲
    MOTION_CALIB_RUNNING,  // 正在采集
    MOTION_CALIB_DONE,     // 成功完成
    MOTION_CALIB_FAILED,   // 检测到剧烈晃动
} motion_calib_state_t;
```

#### 超时保护机制

当 MPU-9250 硬件不可用或 I2C 通信失败时，校准算法无法收到数据帧，
状态永远停在 `RUNNING`，导致 overlay 永不销毁、用户无法退出仪表盘。

修复方案：在校准开始时记录 `s_calib_start_at` 时间戳，
`MOTION_CALIB_RUNNING` 分支中检测超时：

| 阶段 | 时长 | 行为 |
|------|------|------|
| 超过 15s | 触发超时 | 显示 "Timeout! No IMU data" + 红色进度环 |
| 再过 2s | 自动销毁 | `destroy_calib_overlay()` 清理状态 |

> 超时后用户可正常短按退出仪表盘。

### 4.6 表情引擎联动 (emote_engine.c)

| 粉尘浓度 | 表情 | 优先级 | 规则名称 |
|---------|------|--------|---------|
| ≥ 0.25 mg/m³ | `sad_05s15s` (悲伤) | 85 | Dust Severe |
| ≥ 0.15 mg/m³ | `sigh_20s_40s` (叹气) | 75 | Dust Moderate |

### 4.7 Mesh 协议传输

粉尘数据通过 `sensor_packet_t` 传输到 B板 (PathFinder_Tracker)：

```c
// mesh_protocol.h
typedef struct {
    /* ... */
    float    dust_density;   // mg/m³
    uint8_t  dust_aqi;       // 0~4
    uint8_t  flags;          // bit5 = Dust available
    /* ... */
} sensor_packet_t;

// main.c 发送时设置
pkt.dust_density = env.dust.density_mgm3;
pkt.dust_aqi     = env.dust.aqi_level;
pkt.flags |= 0x20;  // Dust available
```

---

## 5. A板 GPIO 占用总览

| GPIO | 功能 | 模块 |
|------|------|------|
| GPIO1 | ADC1_CH0 (粉尘 Vo) | drv_dust_gp2y |
| GPIO3 | ADC1_CH2 (UV sensor) | drv_uv_adc |
| GPIO8 | ADC1_CH7 (UV sensor) | drv_uv_adc |
| GPIO39 | 粉尘 LED 脉冲控制 | drv_dust_gp2y |
| GPIO40 | TWAI TX (CAN) | obd_manager → SN65HVD230 CTX |
| GPIO38 | TWAI RX (CAN) | obd_manager ← SN65HVD230 CRX |
| GPIO13 | I2C SDA | AHT20/BMP280/QMC5883L |
| GPIO20 | I2C SCL | AHT20/BMP280/QMC5883L |
| GPIO0–7 | RGB LCD 并行数据 | ST7701 LCD |
| GPIO9–12, 14–21 (部分) | RGB LCD 控制 | ST7701 LCD |

---

## 6. 构建与烧录

```bash
# 设置 ESP-IDF 环境
source ~/esp/esp-idf/export.sh

# 进入项目目录
cd PathFinder_EMOTE

# 编译
idf.py build

# 烧录到 A板 (需手动按 BOOT+RESET 进入下载模式)
# 注意：CH343 串口必须使用 stub 模式 (不加 --no-stub)，否则数据会损坏
python -m esptool --chip esp32s3 -p /dev/cu.wchusbserial5AF61192361 \
  -b 115200 --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/pathfinder_emote.bin

# 验证固件完整性 (必做！CH343 数据损坏频发)
python -m esptool --chip esp32s3 -p /dev/cu.wchusbserial5AF61192361 \
  -b 115200 verify-flash 0x10000 build/pathfinder_emote.bin
```

> **CH343 烧录注意事项：**
> 1. **必须使用 stub 模式**：不加 `--no-stub`，stub 提供压缩传输和自动校验
> 2. **波特率限 115200**：CH343 芯片在高速传输时不稳定，460800 会数据损坏
> 3. **每次烧录后必须 verify-flash**：`Hash of data verified` 不代表 flash 内容正确
> 4. 如验证失败：先 `erase-region 0x10000 0x250000` 再重新烧录
> 5. **烧录后**：按一次 RESET 键退出下载模式

---

## 7. 车辆 OBD-II 接入

通过 SN65HVD230 CAN 收发器 + ESP32-S3 TWAI 控制器，以 ISO 15765-4（CAN 11-bit / 500 kbps）
协议读取车辆标准 OBD-II 数据，在 OBD 仪表页显示转速/车速/水温等。
页面轮转：表情页 → (点击) → 飞行仪表页 → (点击) → OBD 仪表页 → (点击) → 回表情页。

### 7.1 SN65HVD230 ↔ ESP32-S3 接线表

```
ESP32-S3 (TK021F2699)          SN65HVD230 模块
┌─────────────────┐            ┌──────────────┐
│ 3V3  ───────────┼────────────┤ VCC (3.3V)   │
│ GND  ───────────┼────────────┤ GND          │
│ GPIO40 (TWAI TX)┼────────────┤ CTX / D      │
│ GPIO38 (TWAI RX)┼────────────┤ CRX / R      │
└─────────────────┘            │ CANH ────────┼──→ OBD DLC Pin 6
                               │ CANL ────────┼──→ OBD DLC Pin 14
                               └──────────────┘
```

**注意事项：**

1. SN65HVD230 必须 **3.3V 供电**（芯片非 5V 器件）
2. 模块板载 120Ω 终端电阻建议断开（车辆总线两端已有终端，等效 60Ω）；
   若为不可拆模块可先直接测试，通信不稳再处理
3. 双绞线连接 CANH/CANL，长度尽量短（<1m）
4. GPIO38/GPIO40 为全代码零引用的空闲引脚；**禁用 GPIO12/14**（实测被 LCD 信号间接占用，接外设即黑屏）
5. **检查 RS 脚（Pin 8）**：必须接地或经 10kΩ 下拉（多数模块板载 10kΩ，默认斜率控制模式，可直接用）。
   若 RS 悬空或被拉高（>0.75×VCC）芯片进入 Standby 只听模式，**只能收不能发**，表现为永久离线

### 7.2 OBD-II DLC（车内 16 针诊断口）接线图

```
OBD-II 母座（面向插口视角）
 ┌───────────────────────────┐
  \  1  2  3  4  5  6  7  8 /      Pin 4  = 底盘地 (Chassis GND)
   \                       /       Pin 5  = 信号地 (Signal GND)
    \ 9 10 11 12 13 14 15 16/      Pin 6  = CAN_H (ISO 15765-4)
     └─────────────────────┘       Pin 14 = CAN_L (ISO 15765-4)
                                   Pin 16 = +12V 常电 (蓄电池)
```

- **CANH → Pin 6，CANL → Pin 14**（2008 年后车辆强制支持 CAN 500kbps）
- **共地**：若 ESP32 独立供电（充电宝），须将 GND 与 Pin 4/5 连通；
  若从 Pin 16 取电（12V→5V DC-DC 降压模块给开发板 5V 输入），天然共地。
  Pin 16 为常电，熄火后需拔下以免耗蓄电池

### 7.3 支持的 PID 列表

| PID | 项目 | 轮询周期 | 公式 |
|-----|------|---------|------|
| 0x0C | 发动机转速 | 500ms (2Hz) | (256A+B)/4 [rpm]，EMA α=0.3 |
| 0x0D | 车速 | 500ms (2Hz) | A [km/h] |
| 0x05 | 冷却液温度 | 2000ms | A-40 [°C] |
| 0x11 | 节气门开度 | 1000ms | A×100/255 [%] |
| 0x42 | 控制模块电压 | 2000ms | (256A+B)/1000 [V] |
| 0x0F | 进气温度 | 2000ms | A-40 [°C] |

### 7.4 软件架构

- **obd_manager.c**：TWAI 驱动 (IDF 6.0 `esp_driver_twai` node API)，串行请求-响应
  （**物理寻址 ID=0x7E0** 仅请求 ECM，硬件过滤只收 0x7E8~0x7EF）；任务绑 Core 1，
  TWAI 中断与 Core 0 的 RGB LCD DMA 中断物理隔离
- **总线安全设计**：
  - 帧间保护间隔 20ms，避免连续请求淹没 CAN 总线
  - 总请求 ≤6 帧/s（总线负载率 <1%），不干扰 ECU 自身通信
  - 连续 TX 失败 3 次自动退避 1s
  - bus-off 后强制冷却 5s 再恢复
  - 离线探测周期 3s，降低对车辆 CAN 总线的干扰
- **obd_dashboard.c**：OBD 仪表覆盖层（中央转速表 + 车速/水温/电压），
  三级节流 50/200/1000ms
- **离线降级**：连续 10 次超时转离线，每 3s 发探测帧（PID 0x00）自动恢复；
  未接模块时仪表页显示“OBD 未连接”，不影响其他功能
- **Kconfig 开关**：`Example Configuration → Enable OBD-II vehicle data via TWAI/SN65HVD230`

> ⚠️ **车辆兼容性说明**：原设计使用 0x7DF 功能寻址广播请求（所有 ECU 被迫响应），
> 在部分车型（如斯巴鲁傲虎 2011 款）上会触发 U 类通信故障码。
> 现已改为 0x7E0 物理寻址（仅 ECM 响应）+ 低频轮询，显著降低对车辆总线的干扰。

---

## 8. 参考文档

| 文档 | 来源 | 链接 |
|------|------|------|
| DFRobot Wiki (SEN0144) | DFRobot 官方 | https://wiki.dfrobot.com/sen0144/ |
| Arduino 示例代码 | DFRobot Wiki | https://wiki.dfrobot.com/sen0144/docs/18418 |
| 产品页 (中文) | DFRobot 中国 | https://www.dfrobot.com.cn/goods-827.html |
| 产品页 (英文) | DFRobot Global | https://www.dfrobot.com/product-1063.html |
| 原理图 PDF | DFRobot 中国 | https://www.dfrobot.com.cn/images/upload/File/20131204142145xa068u.pdf |
| Sharp 应用笔记 | Sharp 官方 | https://global.sharp/products/device/lineup/data/pdf/datasheet/gp2y1010au_appl_e.pdf |
| Chris Nafis 公式 | 开源社区 | http://www.howmuchsnow.com/arduino/airquality/ |

---

## 9. 变更日志

### 2026-07-23: ADC1 handle 共享 + UI 更新 + 烧录修正

#### Bug 修复

1. **ADC1 handle 冲突**：`drv_dust_init` 和 `drv_uv_init` 都尝试创建 ADC1 oneshot handle，
   导致粉尘传感器初始化失败 (`ESP_ERR_INVALID_STATE`)，所有读取返回错误。
   - 修复：粉尘驱动改为接收外部共享 handle，由 `drv_uv_get_adc_handle()` 提供。
   - 涉及文件：`drv_dust_gp2y.h/c`、`drv_uv_adc.h/c`、`sensor_manager.c`

2. **浓度公式阈值门控**：原实现中 Vo < 0.6V 时浓度直接返回 0，
   但干净空气 Vo 通常为 0.3~0.5V，导致浓度恒为 0。
   - 修复：移除 0.6V 阈值，始终计算 `density = 0.17 * Vo - 0.1`，负值裁剪到 0。

#### UI 更新

3. **环境明细页字体统一**：
   - 标题：`montserrat_20` → `montserrat_24`（与右侧数值一致）
   - 左侧标签：`montserrat_14` 灰色 → `montserrat_18` 纯白
   - Dust 行新增电压显示（`0.08  0.85V` 格式），便于硬件诊断

#### 烧录方法修正

4. **CH343 stub 模式**：`--no-stub` 模式下 CH343 数据损坏率极高
   （`Hash of data verified` 后 `verify-flash` 仍 digest mismatch）。
   - 修正：移除 `--no-stub`，启用 stub 压缩传输 + 每次烧录后 `verify-flash`。
   - 波特率：460800 → 115200
   
   ---
   
   ### 2026-07-23: 校准弹窗字体统一 + 飞行仪表盘超时保护
   
   #### UI 更新
   
   5. **环境明细页交互控件字体统一**：
      - CAL 按钮：`montserrat_14` → `montserrat_24` 纯白，尺寸 90×34 → 120×44
      - `< tap to back`：`montserrat_14` 灰色 → `montserrat_20` 纯白
      - Adjust 标签：`montserrat_14` → `montserrat_22` 纯白
      - `-10m` / `+10m` 按钮：新增 `montserrat_20` 纯白
      - `OK` / `RESET` 按钮：新增 `montserrat_20` 纯白
      - 海拔显示：`montserrat_20` 蓝色 → `montserrat_22` 纯白
      - P0 显示：`montserrat_16` 灰色 → `montserrat_18` 纯白
      - sdkconfig 新增启用 `CONFIG_LV_FONT_MONTSERRAT_22=y`
   
   #### Bug 修复
   
   6. **飞行仪表盘校准弹窗无法退出**：
      - 根因：MPU-9250 读取失败 → 校准状态永远停在 `RUNNING` →
        `s_calib_overlay` 永不销毁 → `att_page_click_cb` 中 `if (s_calib_overlay) return`
        永远阻止退出。
      - 修复：在 `MOTION_CALIB_RUNNING` 分支添加 15s 超时保护，
        显示 "Timeout! No IMU data" 后 2s 自动销毁 overlay。
      - 涉及文件：`flight_instruments.c`
