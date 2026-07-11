# PathFinder_EMOTE 传感器 + 动态表情联动 设计方案

> Phase 1：环境传感器 + 姿态传感器 + 动态表情联动
> 日期：2026-07-11

---

## 1. 概述

### 1.1 目标

将 PathFinder_EMOTE 从极简 EAF 表情播放器升级为**车载智能表情终端**：

- 集成 AHT20+BMP280（温湿度气压）、GUVA-S12SD（UV）、MPU-6050（6DOF 姿态）
- 实时显示温湿度/气压/海拔/UV 指数/车身倾角
- 基于车辆动态（加减速、转弯、坡度、颠簸、碰撞）智能切换表情动画
- 为 Phase 2（OV2640 摄像头 + ES8311 音频 + xiaozhi-esp32 语音助手）预留扩展空间

### 1.2 硬件平台

| 组件 | 型号 | 说明 |
|------|------|------|
| 主控 | ESP32-S3-WROOM-1 | 16MB Flash, Octal PSRAM 80MHz |
| LCD | ST7701S (TK021F2699) | 480×480 RGB565 圆形屏 |
| 触摸 | CST3530 | 电容触摸, I2C 地址 0x58 |
| 温湿度+气压 | AHT20+BMP280 一体模块 | I2C, AHT20@0x38 + BMP280@0x76 |
| 姿态 | GY-521 (MPU-6050) | 6DOF 加速度+陀螺仪, I2C@0x68 |
| 紫外线 | GUVA-S12SD | 模拟输出, ADC |

### 1.3 已有 EAF 动画资源（23 个）

| 序号 | 名称 | 含义 | | 序号 | 名称 | 含义 |
|------|------|------|---|------|------|------|
| 0 | angry_20s | 生气 | | 12 | ponder_05s | 沉思 |
| 1 | asleep_215s | 睡着 | | 13 | question_05s | 疑问 |
| 2 | badminton_12 | 打羽毛球 | | 14 | sad_05s15s | 悲伤 |
| 3 | confident_08 | 自信 | | 15 | shocked_05s_ | 震惊 |
| 4 | cry_10s_20s | 哭泣 | | 16 | shy_20s_40s | 害羞 |
| 5 | investigate_ | 调查/专注 | | 17 | sigh_20s_40s | 叹气 |
| 6 | laugh_05s_10 | 大笑 | | 18 | smile_05s | 微笑 |
| 7 | leisure_05s_ | 休闲 | | 19 | smile_static | 微笑(静) |
| 8 | mock_05s | 嘲讽 | | 20 | snigger_10s | 窃笑 |
| 9 | music_25s | 听音乐 | | 21 | yawn_20s | 打哈欠 |
| 10 | mute_05s | 无语 | | 22 | yummy_20_s | 享受 |
| 11 | panic_05s_15 | 恐慌 | | | | |

共 23 个动画（序号 0-22）。

---

## 2. 硬件层 — GPIO 分配与 I2C 总线

### 2.1 已占用引脚

| 引脚 | 用途 | 引脚 | 用途 |
|------|------|------|------|
| 0 | R0(LCD)/BOOT | 18 | G3(LCD) |
| 2 | PCLK(LCD) | 21 | R4(LCD) |
| 4-7 | B0-B3(LCD) | 38 | WS2812 |
| 9-11 | G4-G5(LCD) | 41 | HSYNC(LCD) |
| 13 | SCL(Touch I2C-0) | 42 | DE(LCD) |
| 15-17 | B4/G0-G1(LCD) | 45-48 | R1-R3/G6-G7(LCD) |
| 20 | SDA(Touch I2C-0) | 8 | Touch INT |

### 2.2 新增传感器引脚

```
传感器 I2C-1 总线（独立于触摸 I2C-0）：
  SCL = GPIO 12
  SDA = GPIO 14

I2C-1 总线设备：
  ├── AHT20      @ 0x38   (温湿度)
  ├── BMP280     @ 0x76   (气压/海拔)
  └── MPU6050    @ 0x68   (6DOF 加速度+陀螺仪)

UV 模拟传感器：
  ADC1_CH3 = GPIO 3   (GUVA-S12SD 模拟输出)
```

### 2.3 预留引脚（Phase 2 音视频/xiaozhi）

GPIO 19, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 39, 40, 43, 44

### 2.4 设计要点

- **I2C-1 与触摸 I2C-0 完全隔离**：避免 LCD SPI 复用引脚时序干扰
- **三个 I2C 传感器共用总线**：标准 400kHz，带宽充裕
- **UV 用 ADC1**：ADC1 在 Wi-Fi 开启时仍可正常工作（为 Phase 2 xiaozhi 预留）
- **GPIO 3 选型**：ADC1_CH3，不与任何 LCD/触摸引脚冲突

---

## 3. 驱动层 — 传感器驱动组件

### 3.1 文件结构

```
main/
├── drivers/                    ← 新增驱动目录
│   ├── drv_aht20.h             温湿度驱动
│   ├── drv_aht20.c
│   ├── drv_bmp280.h            气压/海拔驱动
│   ├── drv_bmp280.c
│   ├── drv_mpu6050.h           6DOF 姿态驱动
│   ├── drv_mpu6050.c
│   ├── drv_uv_adc.h            UV 模拟读取
│   └── drv_uv_adc.c
├── sensor_manager.h            ← 传感器管理器
├── sensor_manager.c
├── motion_engine.h             ← 运动分析引擎
├── motion_engine.c
├── emote_engine.h              ← 表情联动引擎
├── emote_engine.c
├── app_emote_assets.h          (已有)
├── app_emote_assets.c          (已有)
├── LCD.h / LCD.c               (已有)
└── main.c                      (重构)
```

### 3.2 驱动接口

```c
/* drv_aht20.h */
typedef struct {
    float temperature;  /* °C */
    float humidity;     /* %RH */
} aht20_data_t;
esp_err_t drv_aht20_init(i2c_master_bus_handle_t bus, uint8_t addr);
esp_err_t drv_aht20_read(aht20_data_t *out);

/* drv_bmp280.h */
typedef struct {
    float pressure;    /* Pa */
    float temperature; /* °C (芯片内部温度，用于补偿) */
    float altitude;    /* m (基于标准海平面气压换算) */
} bmp280_data_t;
esp_err_t drv_bmp280_init(i2c_master_bus_handle_t bus, uint8_t addr);
esp_err_t drv_bmp280_read(bmp280_data_t *out);

/* drv_mpu6050.h */
typedef struct {
    float accel[3];    /* ax, ay, az 单位 g */
    float gyro[3];     /* gx, gy, gz 单位 °/s */
    float temp;        /* °C */
} mpu6050_data_t;
esp_err_t drv_mpu6050_init(i2c_master_bus_handle_t bus, uint8_t addr);
esp_err_t drv_mpu6050_read(mpu6050_data_t *out);

/* drv_uv_adc.h */
typedef struct {
    uint16_t raw;      /* ADC 原始值 */
    float voltage;     /* 电压 V */
    float uv_index;    /* UV 指数 0-11+ */
} uv_data_t;
esp_err_t drv_uv_init(adc_channel_t channel);
esp_err_t drv_uv_read(uv_data_t *out);
```

### 3.3 设计要点

- **统一 I2C 总线句柄**：sensor_manager 创建 I2C-1 总线后，将 bus handle 传给各驱动 init
- **I2C 新驱动 API**：使用 ESP-IDF `i2c_master`（新 API），与现有触摸驱动风格一致
- **BMP280 海拔**：驱动内部用标准大气压公式 `h = 44330 * (1 - (P/P0)^(1/5.255))`，P0 可配置校准
- **MPU6050 量程**：加速度 ±2g，陀螺仪 ±250°/s（车辆场景足够，精度最优）
- **UV 指数换算**：GUVA-S12SD 输出电压与 UV 指数近似线性，驱动内做标定映射

---

## 4. 传感器管理器 — 采样任务与数据结构

### 4.1 核心数据结构

```c
/* sensor_manager.h */

/* 环境数据快照（低频更新 1Hz） */
typedef struct {
    aht20_data_t  aht20;    /* 温湿度 */
    bmp280_data_t bmp280;   /* 气压/海拔 */
    uv_data_t     uv;       /* UV指数 */
    int64_t       timestamp_us;
} env_snapshot_t;

/* IMU 原始数据（高频 50Hz，只保持最新值） */
typedef struct {
    mpu6050_data_t imu;
    int64_t        timestamp_us;
} imu_snapshot_t;
```

### 4.2 任务架构

```
env_task (4KB, prio=3, @1Hz)
├── 每 1s 轮询 AHT20 → BMP280 → UV
├── 更新 s_env_snapshot（互斥锁保护）
└── 通知 UI 叠加层刷新

imu_task (6KB, prio=4, @50Hz / 20ms)
├── 每 20ms 读取 MPU6050
├── 更新 s_imu_snapshot（互斥锁保护）
└── 将数据喂给 motion_engine_process()
```

### 4.3 API

```c
esp_err_t sensor_manager_init(i2c_master_bus_handle_t bus);
esp_err_t sensor_manager_get_env(env_snapshot_t *out);
esp_err_t sensor_manager_get_imu(imu_snapshot_t *out);
```

### 4.4 设计要点

- **两个独立任务**：环境传感器 1Hz vs IMU 50Hz，CPU 占用最优化
- **互斥锁保护**：env/imu 各一把互斥锁，读取快、冲突极低
- **IMU 不缓存历史**：运动分析引擎内部维护滑动窗口，传感器管理器只负责"最新值"
- **优先级**：imu_task(4) > env_task(3) > LVGL(1)，保证 IMU 采样不被 UI 阻塞

---

## 5. 运动分析引擎 — 姿态检测算法

### 5.1 运动事件类型

```c
typedef enum {
    MOTION_IDLE = 0,        /* 静止 */
    MOTION_CRUISE,          /* 匀速行驶 */
    MOTION_ACCEL,           /* 急加速 */
    MOTION_BRAKE,           /* 急刹车 */
    MOTION_TURN_LEFT,       /* 急左转 */
    MOTION_TURN_RIGHT,      /* 急右转 */
    MOTION_UPHILL,          /* 上坡 */
    MOTION_DOWNHILL,        /* 下坡 */
    MOTION_TILT_LEFT,       /* 左倾斜 */
    MOTION_TILT_RIGHT,      /* 右倾斜 */
    MOTION_BUMPY,           /* 颠簸 */
    MOTION_COLLISION,       /* 碰撞/冲击 */
    MOTION_HIGH_SPEED,      /* 高速行驶 */
} motion_event_t;
```

### 5.2 检测算法

**① 加减速检测**（基于 X 轴加速度变化率）

安装假设：MPU6050 X 轴朝车头方向

- 急加速：ax > +0.3g 持续 > 200ms
- 急刹车：ax < -0.3g 持续 > 200ms
- 正常行驶：|ax| < 0.1g → MOTION_CRUISE
- 静止：|ax|+|ay|+|az-1g| < 0.05g 持续 > 1s → MOTION_IDLE

**② 急转弯检测**（基于 Z 轴角速度）

- 急左转：gz > 30°/s 持续 > 300ms
- 急右转：gz < -30°/s 持续 > 300ms

**③ 坡度与倾斜**（基于重力分量分解）

- 俯仰角 pitch = atan2(ax, sqrt(ay² + az²)) → 上坡/下坡
- 横滚角 roll  = atan2(ay, sqrt(ax² + az²)) → 左倾/右倾
- |pitch| > 10° → UPHILL / DOWNHILL
- |roll|  > 10° → TILT_LEFT / TILT_RIGHT
- |roll|  > 25° → 高优先级（紧张表情）

**④ 颠簸与碰撞**（基于加速度高频分量 + 峰值检测）

- 颠簸：总加速度 |a| - 1g 的方差 > 0.15g²（滑动窗口 500ms）
- 碰撞：瞬时 |a| > 2.5g（单帧即触发）

**⑤ 高速行驶检测**（基于综合运动强度）

- 综合加速度 |a| 持续 > 0.5g 超过 3s → MOTION_HIGH_SPEED
- 实际场景中，持续高加速度 = 高速行驶状态
- 此状态为持续性状态（非瞬态事件），hold_ms=0 表示持续显示

### 5.3 事件优先级仲裁

碰撞(15) > 急刹车(14) > 急加速(13) > 急转弯(12) > 颠簸(10) > 坡度倾斜(8) > 行驶状态(5) > 静止(1)

同一时刻只输出最高优先级事件，避免表情频繁切换。

### 5.4 API

```c
esp_err_t motion_engine_init(void);
motion_event_t motion_engine_process(const mpu6050_data_t *imu);
void motion_engine_get_angles(float *pitch_deg, float *roll_deg);
```

### 5.5 设计要点

- **滑动窗口**：维护最近 25 帧（500ms@50Hz）用于颠簸方差计算
- **防抖机制**：事件切换需满足持续时长阈值，避免噪声导致表情闪烁
- **安装方向校准**：提供 `motion_engine_calibrate()` 在静止水平状态校准零偏
- **可调阈值**：所有检测阈值用结构体封装，支持运行时调整

---

## 6. 表情联动引擎 — 事件→表情映射

### 6.1 映射规则表

```c
typedef struct {
    motion_event_t  trigger;       /* 触发事件 */
    const char     *emote_name;    /* 对应动画名 */
    uint32_t        hold_ms;       /* 持续播放时长 */
    uint8_t         priority;      /* 优先级 0-15 */
    bool            interrupt;     /* 是否立即打断当前表情 */
} emote_rule_t;

static const emote_rule_t s_rules[] = {
    { MOTION_COLLISION,   "shocked_05s_",  3000, 15, true  },
    { MOTION_BRAKE,       "panic_05s_15",  2000, 14, true  },
    { MOTION_ACCEL,       "confident_08",  2000, 13, true  },
    { MOTION_TURN_LEFT,   "laugh_05s_10",  1500, 12, true  },
    { MOTION_TURN_RIGHT,  "laugh_05s_10",  1500, 12, true  },
    { MOTION_BUMPY,       "angry_20s",     2000, 10, false },
    { MOTION_DOWNHILL,    "snigger_10s",   3000,  8, false },
    { MOTION_UPHILL,      "investigate_",  3000,  8, false },
    { MOTION_TILT_LEFT,   "mute_05s",      2000,  6, false },
    { MOTION_TILT_RIGHT,  "mute_05s",      2000,  6, false },
    { MOTION_HIGH_SPEED,  "yummy_20_s",       0,  5, false },
    { MOTION_CRUISE,      "leisure_05s_",     0,  3, false },
    { MOTION_IDLE,        "asleep_215s",      0,  1, false },
};
```

### 6.2 状态机行为

1. 收到 motion_event → 查表找到匹配规则
2. 若新优先级 >= 当前优先级 OR 当前表情已过期 → 切换表情，设置 hold_ms 倒计时
3. 若新优先级 < 当前优先级 → 忽略（高优先级表情未播完）
4. hold_ms 到期后 → 自动回退到当前 motion_event 对应的表情

### 6.3 动画名称容错策略

1. 尝试用 emote_name 在 app_emote_assets 中查找（精确匹配）
2. 若找不到，遍历所有动画名，查找包含 emote_name 前缀的条目（如 "shocked" 匹配 "shocked_05s_"）
3. 若仍找不到，回退到序号轮播模式（使用当前手动轮播位置的动画）

### 6.4 线程安全设计

- `emote_engine_on_motion()` 在 imu_task（非 LVGL 线程）被调用 → 只更新 pending_event 标志
- `emote_engine_tick()` 在 LVGL 定时器（LVGL 线程）被调用 → 读取 pending_event，执行实际表情切换
- **所有 LVGL/EAF 操作只在 LVGL 线程执行**，避免并发问题

### 6.5 手动点击行为

中心 EAF 区域点击 → 手动轮播（切换到未分配动画列表），不触发运动表情覆盖。
运动表情播完后自动回退到手动轮播的当前动画。

### 6.6 未分配动画（保留给手动轮播 + 后续语音对话）

badminton_12, cry_10s_20s, mock_05s, music_25s, ponder_05s, question_05s, sad_05s15s, shy_20s_40s, sigh_20s_40s, smile_05s, smile_static, yawn_20s

---

## 7. UI 层 — 数据叠加层设计

### 7.1 界面布局（480×480 圆形屏）

```
┌─────────────── 480 × 480 圆形屏 ───────────────┐
│                                                  │
│   ┌──────────┐                    ┌──────────┐  │
│   │ 26.5°C   │                    │ 1013hPa  │  │
│   │ 45% RH   │                    │ 128m     │  │
│   └──────────┘                    └──────────┘  │
│                                                  │
│               ┌─────────────┐                    │
│               │             │                    │
│               │   EAF 表情   │  ← 全屏居中        │
│               │   (核心区域)  │                    │
│               │             │                    │
│               └─────────────┘                    │
│                                                  │
│   ┌──────────┐                    ┌──────────┐  │
│   │ UV: 6.2  │       ╱ ╲          │  倾角     │  │
│   │ 强        │      ╱   ╲         │  ↗ +12°   │  │
│   └──────────┘                    └──────────┘  │
│                                                  │
└──────────────────────────────────────────────────┘
```

### 7.2 叠加卡片

| 卡片 | 位置 | 内容 | 更新频率 |
|------|------|------|---------|
| 左上 | (12, 60) | 温度 + 湿度 | 1Hz |
| 右上 | 右对齐(12, 60) | 气压 + 海拔 | 1Hz |
| 左下 | (12, 底部-90) | UV 指数 + 等级 | 1Hz |
| 右下 | 右对齐(底部-90) | 倾角指示器(pitch/roll) | 10Hz |

### 7.3 样式规范

```c
/* 半透明黑底 + 白色小字 */
lv_obj_set_style_bg_color(card, lv_color_black(), 0);
lv_obj_set_style_bg_opa(card, LV_OPA_50, 0);        /* 50% 透明 */
lv_obj_set_style_radius(card, 12, 0);                /* 圆角 */
lv_obj_set_style_pad_all(card, 6, 0);
lv_obj_set_style_text_font(card, &lv_font_montserrat_14, 0);
lv_obj_set_style_text_color(card, lv_color_white(), 0);
```

### 7.4 倾角指示器（右下卡片）

- 画一条水平基准线 + 一条随 roll 角度旋转的线
- 颜色随倾斜程度变化：绿色(<10°) → 黄色(10-20°) → 红色(>20°)
- pitch 角度以数字 + 上/下箭头显示

### 7.5 与表情引擎的集成

```
LVGL 主任务循环:
  1. lv_timer_handler()  → 处理 LVGL 事件
  2. emote_engine_tick() → 处理待切换表情（过期检测）
  3. overlay_update()    → 从 sensor_manager 读最新数据，更新卡片内容
     ├── env 数据 @1Hz 节流（避免每帧都读 I2C）
     └── 倾角数据 @10Hz（从 motion_engine 缓存读取，不额外 I2C）
```

---

## 8. 任务架构总览

### 8.1 完整初始化流程

```
app_main()
  ├── Lcd_Initialize()           ← ST7701S SPI 初始化（已有）
  ├── RGB Panel 安装              ← 双帧缓冲 + Bounce Buffer（已有）
  ├── lv_init() + Display 注册    ← 已有
  ├── touch_init()                ← CST3530 I2C-0（已有）
  │
  ├── [新增] I2C-1 总线安装       ← GPIO 12(SCL) + 14(SDA)
  ├── [新增] sensor_manager_init  ← 初始化 4 个驱动 + 2 个任务
  │     ├── drv_aht20_init()
  │     ├── drv_bmp280_init()
  │     ├── drv_mpu6050_init()
  │     ├── drv_uv_init()
  │     ├── env_task  @1Hz  (4KB, prio=3)
  │     └── imu_task   @50Hz (6KB, prio=4)
  │           └── motion_engine_process()
  │
  ├── [新增] motion_engine_init   ← 运动分析引擎
  ├── [新增] emote_engine_init    ← 绑定 EAF widget
  ├── app_emote_assets_init()     ← 已有
  ├── ui_create()                 ← 重构：EAF + 叠加卡片
  │
  └── lvgl_task()                 ← 已有（增大栈至 12KB）
        ├── lv_timer_handler()
        ├── emote_engine_tick()   ← [新增]
        └── overlay_update()      ← [新增]
```

### 8.2 RTOS 资源清单

| 资源 | 数量 | 用途 |
|------|------|------|
| 任务 | 3 个 | lvgl_task(12KB/prio1) + env_task(4KB/prio3) + imu_task(6KB/prio4) |
| 互斥锁 | 3 个 | lvgl_mux + env_mux + imu_mux |
| 定时器 | 2 个 | lvgl_tick(2ms) + emote_expire(检查) |
| I2C 总线 | 2 个 | I2C-0(触摸) + I2C-1(传感器) |

### 8.3 PSRAM 使用预估

| 组件 | 大小 | 来源 |
|------|------|------|
| RGB 双帧缓冲 | ~900KB | 已有 |
| LVGL 堆 | ~100KB | 已有 |
| 运动分析滑动窗口 | ~600B | 新增 |
| 总计 | ~1MB | 8MB PSRAM 充裕 |

### 8.4 CMakeLists.txt 更新

```cmake
idf_component_register(
    SRCS
        "main.c"
        "LCD.c"
        "app_emote_assets.c"
        "drivers/drv_aht20.c"
        "drivers/drv_bmp280.c"
        "drivers/drv_mpu6050.c"
        "drivers/drv_uv_adc.c"
        "sensor_manager.c"
        "motion_engine.c"
        "emote_engine.c"
    INCLUDE_DIRS
        "."
        "drivers"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_adc
        esp_lcd
        esp_timer
)
```

---

## 9. Phase 2 扩展预留

### 9.1 MPU-6050 → MPU-9255 升级

```c
/* 驱动层接口已预留磁力计扩展 */
typedef struct {
    float accel[3];
    float gyro[3];
    float mag[3];     /* MPU-9255 磁力计（MPU-6050 填 0） */
    float temp;
} imu_data_t;
/* 升级时只需改 drv_mpu6050.c → drv_mpu9255.c，上层零改动 */
```

新增运动事件（MPU-9255 专属）：
- MOTION_NORTH / MOTION_SOUTH 等指南针方向检测
- MOTION_MAGNETIC_ANOMALY 磁场异常

### 9.2 OV2640 + ES8311 + xiaozhi-esp32

- GPIO 19/26/27/28/29/30/31/32/33/34/35/36/37/39/40/43/44 已预留
- xiaozhi 需要 Wi-Fi（sdkconfig 已支持）、I2S 音频、摄像头 DVP
- 表情引擎扩展：语音对话 TTS 播报时显示 music_25s，AI 回复时显示 ponder_05s

---

## 10. 约束与风险

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| I2C-1 传感器与 RGB LCD 共用 PSRAM | 高优先级 IMU 采样可能影响 LCD 帧率 | imu_task 栈放 PSRAM，减少内部 SRAM 争用 |
| MPU6050 安装方向不确定 | 加减速/转弯检测方向可能反转 | 提供运行时校准功能 |
| 23 个动画可能不完全匹配所有状态 | 部分状态表情不够丰富 | 容错策略 + 后续可扩充动画包 |
| 50Hz IMU 采样 + 运动分析 CPU 开销 | 可能影响 LVGL 渲染 | imu_task 优先级最高，LVGL 用 full_refresh 保证帧完整性 |
