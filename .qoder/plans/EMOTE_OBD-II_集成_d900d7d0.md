# PathFinder_EMOTE 车辆 OBD-II 数据读取与仪表页集成方案

## 概要

在 PathFinder_EMOTE（ESP32-S3-N16R8 + TK021F2699 2.1寸圆形 RGB LCD，ESP-IDF 6.0.0，LVGL 8.3.11）上新增：
1. **obd_manager 模块**：通过 SN65HVD230 CAN 收发器 + ESP32-S3 TWAI 控制器，以 ISO 15765-4（CAN 11-bit / 500 kbps）协议轮询标准 OBD-II PID（转速/水温/车速/节气门/电压/进气温）
2. **obd_dashboard 页面**：圆形屏 OBD 仪表覆盖层（中央转速表指针 + 水温条 + 车速/电压数字），复用 flight_instruments 五函数覆盖层模式
3. **页面轮转**：EAF 表情页 → (点击) → 飞行仪表页 → (点击) → OBD 仪表页 → (点击) → 回表情页
4. **接线文档**：README 新增 SN65HVD230 接线表与 OBD-II DLC 16 针接口接线图

**硬性约束（不可违反）**：不修改 LCD 稳定基线 —— `LCD_PCLK_HZ=10MHz`、`bounce_buffer_size_px=480*40`、`LCD_RGB_ISR_IRAM_SAFE`/`RESTART_IN_VSYNC` 保持关闭、不动 sdkconfig 中任何 LCD/PSRAM 项；不触碰 A/B 板 mesh 协议 `sensor_packet_t`。

---

## 硬件接线设计（实施时写入 README.md）

### GPIO 选择依据
全代码扫描确认已占用引脚：RGB LCD 20 根（41/46/42/2 + DATA0-15: 4,5,6,7,15,16,17,18,9,10,11,0,45,48,47,21）、I2C-0 触摸+传感器（13/20）、UV ADC（3）、粉尘（1/39）、UART0（43/44）、Flash（26-32）、Octal PSRAM（33-37）、GPIO12/14 被 LCD 信号间接占用（实测接外设即黑屏，禁用）。
**GPIO38、GPIO40 全代码零引用、非 strapping、无任何冲突 → 选定。**

### SN65HVD230 ↔ ESP32-S3 接线表

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
注意事项：
- SN65HVD230 必须 **3.3V 供电**（芯片非 5V 器件）
- 模块板载 120Ω 终端电阻建议断开（车辆总线两端已有终端，等效 60Ω）；若为不可拆模块可先直接测试，通信不稳再处理
- 双绞线连接 CANH/CANL，长度尽量短（<1m）

### OBD-II DLC（车内 16 针诊断口）接线图

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
- **共地**：若 ESP32 独立供电（充电宝），须将 GND 与 Pin 4/5 连通；若从 Pin 16 取电（12V→5V DC-DC 降压模块给开发板 5V 输入），天然共地。Pin 16 为常电，熄火后需拔下以免耗蓄电池

---

## 代码变更

### 1.【新增】`main/obd_manager.h` + `main/obd_manager.c` — OBD 数据模块

克隆 `sensor_manager` 的"任务 + 互斥锁快照 + getter"模式（参照 [sensor_manager.c](file:///Users/pm/PathFinder_LCD/PathFinder_EMOTE/main/sensor_manager.c) L95-138 的快照读写与 L324 任务创建）。

**头文件接口**（注释风格对齐 sensor_manager.h）：
```c
typedef struct {
    float    rpm;            /* 0x0C: (256A+B)/4 */
    float    speed_kph;      /* 0x0D: A */
    float    coolant_c;      /* 0x05: A-40 */
    float    throttle_pct;   /* 0x11: A*100/255 */
    float    batt_v;         /* 0x42: (256A+B)/1000 */
    float    intake_c;       /* 0x0F: A-40 */
    uint8_t  valid_bits;     /* 每 PID 一位 */
    bool     connected;      /* 总线在线状态 */
    int64_t  timestamp_us;
} obd_snapshot_t;

esp_err_t obd_manager_init(void);              /* 失败仅打日志，不阻塞 */
esp_err_t obd_manager_get(obd_snapshot_t *out);/* 线程安全 getter */
```

**实现要点**：
- **TWAI 驱动**：IDF 6.0.0 已弃用 legacy `driver/twai.h`，使用 `esp_driver_twai` 组件的 **twai_node API**：`twai_new_node_onchip()`（TX=GPIO40, RX=GPIO38, bitrate=500000）→ `twai_node_register_event_callbacks()` → `twai_node_enable()`。硬件过滤器只收 ID 0x7E8~0x7EF（id=0x7E8, mask 放宽低 3 位）
- **RX 回调（ISR 上下文）**：只做 `twai_node_receive_from_isr()` + 帧拷贝投递 FreeRTOS 队列（深度 8），解析全部放任务侧 —— 避免与 LCD bounce-buffer DMA ISR 抢时
- **核绑定（关键）**：`xTaskCreatePinnedToCore(obd_task, "obd", 4096, NULL, 3, NULL, 1)` 绑 **Core 1**，且 **twai_node 创建也在 obd_task 内执行**（TWAI 中断随创建核注册到 Core 1），与 Core 0 的 RGB LCD GDMA/VSYNC 中断物理隔离。**不开启** `CONFIG_TWAI_ISR_IN_IRAM`（保持 sdkconfig 默认）
- **表驱动 PID 调度**：静态 const 表 `{pid, period_ms, resp_len, scale, offset, ema_alpha, dest字段}`：
  | PID | 项目 | 周期 | 公式 |
  |---|---|---|---|
  | 0x0C | 转速 | 100ms (10Hz) | (256A+B)/4，EMA α=0.5 |
  | 0x0D | 车速 | 200ms (5Hz) | A |
  | 0x05 | 水温 | 1000ms | A-40 |
  | 0x11 | 节气门 | 500ms | A*100/255 |
  | 0x42 | 电压 | 1000ms | (256A+B)/1000 |
  | 0x0F | 进气温 | 1000ms | A-40 |
- **请求格式**：串行请求-响应（同一时刻仅一个未决请求），发 ID=0x7DF 8字节 `{0x02, 0x01, PID, 0x55,0x55,0x55,0x55,0x55}`，等待响应 `{len, 0x41, PID, A, B,...}` 超时 100ms；仅处理单帧响应（Mode 01 PID 均为单帧）
- **离线状态机**：连续 10 次超时 → `connected=false`、快照 valid_bits 清零，降级为每 2s 发一次探测帧（PID 0x00 支持列表）；收到任一有效响应 → 恢复在线全速轮询。若 `twai_new_node_onchip` 返回失败（引脚异常等）→ 打 ESP_LOGW 后任务自删，`connected` 恒 false，**不影响任何现有功能**

### 2.【新增】`main/obd_dashboard.h` + `main/obd_dashboard.c` — OBD 仪表覆盖层

克隆 flight_instruments 五函数 API（[flight_instruments.h](file:///Users/pm/PathFinder_LCD/PathFinder_EMOTE/main/flight_instruments.h) L26-48）：`obd_dashboard_create/show/hide/is_visible/update`。

**结构**（参照 [flight_instruments.c](file:///Users/pm/PathFinder_LCD/PathFinder_EMOTE/main/flight_instruments.c) L661-702）：
- create：全屏 480x480 黑底覆盖层挂 `scr` 根对象，默认 `LV_OBJ_FLAG_HIDDEN`
- show：`emote_engine_pause()` + 清 HIDDEN；hide：加 HIDDEN + `emote_engine_resume()`
- 页面点击（`LV_EVENT_CLICKED`）→ `obd_dashboard_hide()` 回表情页

**单页融合布局**（圆形屏安全区内，禁止新建 canvas —— 姿态页 canvas 已占 320KB PSRAM，转速表用轻量控件）：
- **中央转速表**：lv_arc 静态刻度弧（0-8000 RPM，240° 量程）+ 刻度标签（`lv_label`，创建时一次性布置）+ **指针用 lv_img/lv_line + `lv_obj_set_style_transform_angle`** 旋转（复用 L765-767 横滚指针的成熟模式，局部重绘、渲染成本低）+ 中央大数字 RPM
- **顶部**：车速大字（km/h）
- **底部**：水温 lv_bar（60-130°C 量程，>105°C 变红 `lv_palette_main(LV_PALETTE_RED)`）+ 电压/节气门/进气温小字标签
- **离线态**：`connected=false` 时仪表整体半透明 + 中央显示"OBD 未连接"（用现有 font_hzk 中文字体）
- **update 三级节流**（照抄 L709-714 时间戳模式）：指针+RPM 数字 50ms（20Hz）、车速/节气门 200ms、水温/电压/进气温 1000ms；数值无变化时跳过 `lv_label_set_text` 减少脏区

### 3.【修改】`main/main.c` — 3 处小改（约 8 行）

- `ui_create()` L1418 `flight_instruments_create(scr);` 之后加 `obd_dashboard_create(scr);`
- `lvgl_task` L1454 `flight_instruments_update();` 之后加 `obd_dashboard_update();`
- `app_main()` L1783 `sensor_uplink_task` 创建之后（所有现有模块就绪后）加 `obd_manager_init();`
- 顶部 include 区加 `#include "obd_manager.h"`、`#include "obd_dashboard.h"`

### 4.【修改】`main/flight_instruments.c` — 页面轮转（约 3 行）

`att_page_click_cb`（L556-561）中把 `flight_instruments_hide();` 改为：
```c
flight_instruments_hide();
obd_dashboard_show();   /* 轮转到 OBD 仪表页（离线时显示"OBD 未连接"占位） */
```
文件顶部加 `#include "obd_dashboard.h"`。长按校准逻辑（L540）不动。

### 5.【修改】`main/CMakeLists.txt` — 注册（3 行）

SRCS 加 `"obd_manager.c"`、`"obd_dashboard.c"`；REQUIRES 加 `esp_driver_twai`。

### 6.【修改】`main/Kconfig.projbuild` — 可选开关（约 8 行）

仿 `HMC5883L_ENABLE` 风格新增：
```
config OBD_TWAI_ENABLE
    bool "Enable OBD-II vehicle data via TWAI/SN65HVD230"
    default y
    help
        通过 SN65HVD230 (TX=GPIO40, RX=GPIO38) 读取车辆 OBD-II 数据。
        未接模块时自动降级为离线状态，不影响其他功能。
```
`obd_manager_init()` 内部用 `#if CONFIG_OBD_TWAI_ENABLE` 包裹主体。

### 7.【修改】`README.md` — 接线文档

新增"车辆 OBD-II 接入"章节，写入上文的 SN65HVD230 接线表、OBD-II DLC 引脚图、供电/共地注意事项与支持的 PID 列表。

---

## 实施顺序与依赖

1. 步骤 1（obd_manager）→ 独立可先行，编译验证 TWAI API 用法
2. 步骤 2（obd_dashboard）依赖步骤 1 的快照结构
3. 步骤 3、4（集成）依赖 1+2 完成
4. 步骤 5 与步骤 1 同时改（否则编译不过）；步骤 6、7 随时可做

## 验证计划

1. **编译**：`idf.py build`（先 `source export.sh`）确认 esp_driver_twai 链接通过
2. **无模块回归**：烧录后不接 SN65HVD230，确认表情动画/飞行仪表/传感器胶囊/BLE/配网全部正常，LCD 无花屏/黑屏（TWAI init 成功但恒离线），页面轮转：表情→仪表→OBD(显示未连接)→表情
3. **台架测试（可选）**：第二块 ESP32 + SN65HVD230 模拟 ECU（监听 0x7DF、回 0x7E8）验证协议栈
4. **实车测试**：接入 OBD 口，点火后确认怠速转速 ~800 RPM、水温随暖机上升、拔线后 2s 内进入离线态并能自动恢复
5. **稳定性**：OBD 在线 + 转速表 20Hz 刷新下持续 10 分钟观察 LCD 无 FIFO under-run 花屏

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| TWAI 中断落 Core 0 与 LCD GDMA 竞争致花屏 | twai_node 在绑核 Core 1 的 obd_task 内创建；不开 TWAI_ISR_IN_IRAM |
| IDF 6.0 twai_node API 不熟悉 | RX 回调仅 memcpy+队列；编译期即可暴露 API 差异，参考 IDF examples/peripherals/twai |
| GPIO38/40 未从扩展板引出 | 实施前实物确认排针；备选 GPIO8+GPIO19（需实测，GPIO8 历史注释有矛盾） |
| 车辆熄火/未接车时任务空转 | 离线状态机 2s 退避探测；请求超时 100ms 不阻塞 |
| UI 高频更新加剧 PSRAM 带宽压力 | 指针 transform_angle 局部重绘（禁新 canvas）；三级节流；EMA 在数据侧完成使脏区小 |
| 破坏 A/B 板 mesh 协议 | OBD 数据不进 sensor_packet_t，纯本机显示 |

## Rejected Alternatives

- **legacy driver/twai.h**：IDF 6.0 已标记 deprecated 且未来会移除，编译警告污染，node API 是唯一长期路线
- **OBD 数据并入 sensor_manager**：需改 env_snapshot_t、BLE notify、mesh sensor_packet_t（跨板协议），改动面大且 OBD 与 I2C 传感器域无关，独立模块零侵入
- **OBD 页并入 flight_instruments 第三页**：会向已稳定的 800 行文件注入大量代码，违背最小侵入；独立覆盖层仅需 3 行轮转接线
- **lv_meter 控件做转速表**：LVGL 8 的 lv_meter 每帧全表盘重绘，PSRAM 带宽敏感场景成本高；静态弧+transform_angle 指针已被横滚指针实测验证
- **新建 canvas 绘制表盘**：再吃 ~320KB PSRAM 且全量重绘与 LCD GDMA 争带宽，有花屏风险
- **GPIO12/14 作 TWAI 引脚**：TK021F2699 实测该两脚被 LCD 信号间接占用，接外设即黑屏