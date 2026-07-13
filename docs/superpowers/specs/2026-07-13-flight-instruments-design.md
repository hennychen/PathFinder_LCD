# PathFinder EMOTE 仿飞行仪表盘图形化页面设计

## 概述

为 PathFinder_EMOTE 480×480 圆形屏新增仿飞行仪表盘风格的图形化页面，以"姿态指引仪（Artificial Horizon）"和"仿飞行仪表"为核心，直观展示车辆在三维空间中的姿态、海拔与气压状态。

## 需求决策

| 决策项 | 结论 |
|--------|------|
| 指南针数据源 | MPU6050 无磁力计，航向显示 N/A，表盘不旋转。后续升级 MPU9255 时接入磁力计 |
| 交互模式 | 点击表情 → 进入仪表页；点击仪表页 → 返回表情页；长按表情 → 手动切换表情 |
| 页面布局 | 双页滑动切换：第1页全屏姿态指引仪，第2页指南针+海拔计+气压计 |
| 实现方案 | LVGL 原生控件组合（lv_obj + lv_meter + lv_arc + transform_angle） |

## 文件结构

### 新增文件

- `PathFinder_EMOTE/main/flight_instruments.h` — 公开 API
- `PathFinder_EMOTE/main/flight_instruments.c` — 全部仪表实现

### 修改文件

- `PathFinder_EMOTE/main/main.c` — 集成仪表页到交互流程与更新循环
- `PathFinder_EMOTE/main/CMakeLists.txt` — 注册新源文件

## 架构设计

### 模块职责

`flight_instruments.c` 独立管理两个仪表页面的所有 LVGL 对象：
- 从 `sensor_manager` 和 `motion_engine` 读取数据（只读）
- 不操作 EAF/表情引擎，仅负责仪表 UI 的显示/隐藏/更新
- 所有 LVGL 操作在 `lvgl_lock` 内执行（与现有 overlay_update 同一线程）

### 公开 API

```c
// 创建仪表覆盖层（默认隐藏），在 ui_create() 中调用
void flight_instruments_create(lv_obj_t *parent);

// 显示/隐藏仪表页
void flight_instruments_show(void);
void flight_instruments_hide(void);

// 可见性检查
bool flight_instruments_is_visible(void);

// 周期数据更新（在 lvgl_task 中调用，内部 20Hz 限速）
void flight_instruments_update(void);
```

## 第1页：姿态指引仪 (Artificial Horizon)

### LVGL 对象树

```
s_overlay (全屏容器, 480×480, 纯黑背景, 默认隐藏)
└── s_page_att (第1页容器)
    ├── s_att_clip (圆形裁剪容器, 400×400, radius=CIRCLE, clip_corner=true)
    │   └── s_horizon (天空/大地圆盘, 520×520, transform_angle=roll)
    │       ├── s_sky (上半部, 蓝色 #1A6FC4)
    │       ├── s_ground (下半部, 棕色 #8B5E3C)
    │       ├── s_horizon_line (白色地平线, 宽520×2px)
    │       └── s_pitch_ladder (俯仰刻度线容器)
    │           ├── +10°/+20°/+30° 横线 (地平线下方)
    │           └── -10°/-20°/-30° 横线 (地平线上方)
    ├── s_aircraft_symbol (固定飞机符号, 黄色 #FFD700)
    │   ├── 左翼线 (lv_line)
    │   ├── 右翼线 (lv_line)
    │   └── 中心圆 (lv_arc)
    ├── s_roll_arc (顶部横滚刻度弧, lv_arc)
    │   └── 刻度标记: 0°/±10°/±20°/±30°/±45°/±60°
    ├── s_roll_pointer (横滚指针三角, 随roll旋转)
    ├── s_hud_pitch (俯仰数值标签, montserrat_16)
    └── s_hud_roll (横滚数值标签, montserrat_16)
```

### 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 圆形裁剪半径 | 200px | 居中，留出 40px 圆形屏安全区边距 |
| 天空/大地圆盘 | 520×520 | 覆盖旋转后所有可见区域 |
| 俯仰换算 | 4px/度 | ±30° 范围 = ±120px 偏移 |
| 横滚旋转 | `transform_angle(roll * 10)` | LVGL 单位 0.1° |
| 俯仰平移 | `y_offset = pitch * 4` | 正 pitch → 地平线下移 |
| 更新频率 | 20Hz (50ms) | 平衡视觉流畅度与 CPU 开销 |
| 俯仰范围钳制 | ±90° | 超出时锁定，防止天地翻转 |

### 天空/大地实现

- `s_horizon` 是一个 520×520 的 `lv_obj`，居中于裁剪容器
- 内部两个子对象：上半部天空（蓝色 #1A6FC4），下半部大地（棕色 #8B5E3C）
- 通过 `lv_obj_set_style_transform_angle(s_horizon, roll_tenth_deg, 0)` 旋转整个圆盘
- 通过 `lv_obj_align(s_horizon, LV_ALIGN_CENTER, 0, pitch_offset)` 垂直平移

### 飞机符号

- 使用 `lv_line` 绘制左右机翼（黄色 #FFD700，线宽 3px）
- 中心 `lv_arc` 小圆圈（半径 8px）
- 固定在屏幕正中央，不参与旋转

### 横滚刻度弧

- `lv_arc` 绘制半圆弧（半径 180px，位于顶部）
- 刻度标记用 `lv_label` 定位在弧上（0°, 10°, 20°, 30°, 45°, 60° 两侧对称）
- 指针三角形用 `lv_obj` 实现，随 roll 旋转

### 俯仰刻度梯

- 在 `s_horizon` 上绘制水平线，每 10° 一条
- 线条随圆盘一起旋转和平移
- 线条长度：中心 ±60px

## 第2页：指南针 + 海拔计 + 气压计

### LVGL 对象树

```
s_page_gauges (第2页容器)
├── s_compass_container (指南针表盘, 居中偏上, 260×260)
│   ├── s_compass_dial (旋转表盘, radius=CIRCLE, 深色背景)
│   │   ├── N 标签 (红色 #FF5050, montserrat_20, 顶部)
│   │   ├── S 标签 (灰色 #AAAAAA, montserrat_16, 底部)
│   │   ├── E 标签 (灰色 #AAAAAA, montserrat_16, 右侧)
│   │   ├── W 标签 (灰色 #AAAAAA, montserrat_16, 左侧)
│   │   └── 中间刻度数字 (30/60/120/150/210/240/300/330)
│   ├── s_compass_pointer (固定顶部红色三角指针)
│   └── s_compass_hdg_label (中心航向标签 "HDG N/A")
├── s_altimeter (海拔计, lv_meter, 左下, 130×130)
│   ├── 刻度: 0-5000m, 主刻度每1000m
│   ├── 指针: 红色 needle_line
│   └── s_alt_label (中心数值标签, montserrat_20)
├── s_barometer (气压计, lv_meter, 右下, 130×130)
│   ├── 刻度: 960-1060hPa, 主刻度每20hPa
│   ├── 指针: 红色 needle_line
│   └── s_bar_label (中心数值标签, montserrat_20)
└── s_page_indicator (底部页码指示器 "2/2")
```

### 指南针布局

| 参数 | 值 |
|------|-----|
| 表盘中心 | (240, 170) |
| 表盘半径 | 130px |
| N 颜色 | 红色 #FF5050，montserrat_20 |
| 其余方位 | 灰色 #AAAAAA，montserrat_16 |
| 航向显示 | "N/A"（当前无磁力计） |
| 固定指针 | 红色三角，表盘正上方 |

### 海拔计 (lv_meter)

| 参数 | 值 |
|------|-----|
| 中心位置 | (155, 365) |
| 半径 | 65px |
| 刻度范围 | 0 — 5000m |
| 主刻度间隔 | 1000m |
| 刻度颜色 | 深蓝 #00B4FF |
| 指针颜色 | 红色 #FF5050 |
| 数值标签 | montserrat_20 白色，叠加在中心 |

### 气压计 (lv_meter)

| 参数 | 值 |
|------|-----|
| 中心位置 | (325, 365) |
| 半径 | 65px |
| 刻度范围 | 960 — 1060 hPa |
| 主刻度间隔 | 20 hPa |
| 刻度颜色 | 绿色 #00FF88 |
| 指针颜色 | 红色 #FF5050 |
| 数值标签 | montserrat_20 白色，叠加在中心 |

## 交互流程

### 行为变更对照

| 操作 | 当前行为 | 新行为 |
|------|---------|--------|
| 点击 EAF | 切换表情 + 唤出胶囊 | 进入仪表页 |
| 长按 EAF | 无 | 手动切换表情 + 唤出胶囊 |
| 点击仪表页 | 无 | 返回表情页 |
| 左右滑动(仪表页内) | 无 | 切换姿态仪/指南针页 |
| 点击胶囊 | 进入明细页 | 不变（仅胶囊可见时） |

### 页面切换

- 使用 `lv_obj_add_event_cb(LV_EVENT_GESTURE)` 检测左右滑动
- 两个子页面容器通过 `LV_OBJ_FLAG_HIDDEN` 切换可见性
- 底部页码指示器实时更新 "1/2" 或 "2/2"

## main.c 集成

### 修改点

1. **新增 `#include "flight_instruments.h"`**
2. **`click_cb()` 修改**：默认行为改为 `flight_instruments_show()`
3. **新增长按回调** `long_press_cb()`：调用 `emote_engine_manual_next()` + `overlay_show_temporary()`
4. **`ui_create()` 中**：
   - 调用 `flight_instruments_create(scr)` 创建覆盖层（默认隐藏）
   - 为 EAF 对象添加 `LV_EVENT_LONG_PRESSED` 回调
5. **`lvgl_task()` 中**：可见时调用 `flight_instruments_update()`
6. **`overlay_update()` 中**：仪表页可见时跳过胶囊淡出计时

### 数据流

```
flight_instruments_update() 每 50ms (20Hz) 调用:
├── motion_engine_get_angles(&pitch, &roll)
│   ├── → s_horizon transform_angle = roll * 10
│   ├── → s_horizon y_offset = pitch * 4
│   └── → 更新 P/R HUD 标签
├── sensor_manager_get_env(&env)  [仅可见时 @1Hz]
│   ├── → 海拔计 needle 更新 (altitude)
│   ├── → 气压计 needle 更新 (pressure)
│   └── → 更新数值标签
└── sensor_manager_get_imu(&imu)  [预留，当前不旋转指南针]
    └── → 指南针中心显示 "N/A"
```

## 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| `transform_angle` 对大对象(520×520)渲染慢 | 限制 20Hz 更新；圆形裁剪减少实际渲染像素 |
| `clip_corner` 对旋转子对象裁剪不完美 | 备选：用遮罩层裁掉四角 |
| 姿态仪天地翻转时视觉突变（pitch > 90°） | 钳制 pitch 显示范围 ±90°，超出时锁定 |
| 指南针后续接入 MPU9255 需改动 | 预留 `s_compass_dial` 旋转接口，升级时仅需加 `transform_angle` |

## 后续扩展

- **MPU9255 接入**：指南针表盘添加 `transform_angle` 旋转，航向标签显示实际度数
- **第三页面**：可增加速度/温度/UV 等附加仪表
- **动画过渡**：页面切换可添加滑动动画效果
