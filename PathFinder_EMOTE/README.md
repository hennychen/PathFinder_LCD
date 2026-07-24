# PathFinder_EMOTE — 粉尘传感器集成文档

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
Vo ≥ 0.6V:  density (mg/m³) = 0.17 × Vo − 0.1
Vo < 0.6V:  density = 0 (干净空气基线)
```

参考: http://www.howmuchsnow.com/arduino/airquality/ (Chris Nafis (c) 2012)

> 此公式与 DFRobot 官方 Wiki SEN0144 示例代码中使用的公式完全一致。

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
// 初始化
esp_err_t drv_dust_init(adc_unit_t unit, adc_channel_t adc_ch, gpio_num_t led_gpio);

// 读取数据 (10 次脉冲过采样, ~100ms)
esp_err_t drv_dust_read(dust_data_t *out);
```

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
#define DUST_ADC_UNIT     ADC_UNIT_1
#define DUST_ADC_CHANNEL  ADC_CHANNEL_0   /* GPIO1 = ADC1_CH0 */
#define DUST_LED_GPIO     GPIO_NUM_39

// 初始化
drv_dust_init(DUST_ADC_UNIT, DUST_ADC_CHANNEL, DUST_LED_GPIO);

// env_task 中 1Hz 读取
drv_dust_read(&snap.dust);
```

### 4.3 UI 显示 (环境明细页 DETAIL_ENV)

粉尘数据在环境明细页 **第 7 行** 显示：

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
│  Dust    0.08 mg/m³    │  ← 第 7 行
│                         │
└─────────────────────────┘
```

### 4.4 异常告警

```c
#define ALERT_DUST_WARNING  0.15f   // 中度污染阈值

// 当粉尘浓度 ≥ 0.15 mg/m³ 时触发告警
```

### 4.5 表情引擎联动 (emote_engine.c)

| 粉尘浓度 | 表情 | 优先级 | 规则名称 |
|---------|------|--------|---------|
| ≥ 0.25 mg/m³ | `sad_05s15s` (悲伤) | 85 | Dust Severe |
| ≥ 0.15 mg/m³ | `sigh_20s_40s` (叹气) | 75 | Dust Moderate |

### 4.6 Mesh 协议传输

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
python -m esptool --chip esp32s3 -p /dev/cu.wchusbserial5AF61192361 \
  -b 460800 --no-stub --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/pathfinder_emote.bin
```

> **烧录后**：按一次 RESET 键退出下载模式，A板将正常启动。

---

## 7. 参考文档

| 文档 | 来源 | 链接 |
|------|------|------|
| DFRobot Wiki (SEN0144) | DFRobot 官方 | https://wiki.dfrobot.com/sen0144/ |
| Arduino 示例代码 | DFRobot Wiki | https://wiki.dfrobot.com/sen0144/docs/18418 |
| 产品页 (中文) | DFRobot 中国 | https://www.dfrobot.com.cn/goods-827.html |
| 产品页 (英文) | DFRobot Global | https://www.dfrobot.com/product-1063.html |
| 原理图 PDF | DFRobot 中国 | https://www.dfrobot.com.cn/images/upload/File/20131204142145xa068u.pdf |
| Sharp 应用笔记 | Sharp 官方 | https://global.sharp/products/device/lineup/data/pdf/datasheet/gp2y1010au_appl_e.pdf |
| Chris Nafis 公式 | 开源社区 | http://www.howmuchsnow.com/arduino/airquality/ |
