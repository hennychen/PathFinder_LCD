# PathFinder Dashboard — Flutter 数据采集与可视化应用设计

> 日期：2026-07-12
> 状态：已批准
> 关联项目：PathFinder_EMOTE (ESP32-S3 固件)

## 摘要

创建全新的 Flutter Android 应用 `PathFinder_Dashboard`，通过 BLE 低功耗蓝牙连接 ESP32 PathFinder_EMOTE 设备，实时采集环境传感器数据（温度、湿度、气压、海拔、UV）、IMU 运动数据（姿态角、加速度、陀螺仪）、运动事件（碰撞、急刹车、转弯等12种）和表情状态（24种动画），以图表/图形方式展示，并支持本地历史存储与回看。本次范围仅包含 Flutter App 与 BLE 通信协议规范，ESP32 固件 BLE 改造为后续独立任务。

## 1. 需求决策

| 决策项 | 选择 | 理由 |
|--------|------|------|
| 数据来源 | 连接 ESP32 BLE 远程获取 | 真实传感器数据，完整伴侣应用 |
| 通信协议 | BLE 低功耗蓝牙 | 低功耗、手机原生支持、ESP32-S3 内置 |
| 目标平台 | 仅 Android | 精简范围，BLE 支持成熟 |
| 图表类型 | 环境折线图 + IMU 波形 + 事件时间轴 + 表情映射表 | 全覆盖四类数据 |
| 数据存储 | 本地 SQLite (Drift) | 历史回看，无需云端 |
| 表情展示 | 真实动画 (WebP/GIF) + 文字映射 | 视觉还原度最高 |
| 项目范围 | Flutter App + BLE 协议规范 | 固件改造分离，降低耦合 |

## 2. 项目架构

### 2.1 项目位置

`/Users/pm/PathFinder_LCD/PathFinder_Dashboard/`（与 `PathFinder_EMOTE/` 平级）

### 2.2 分层目录结构

```
PathFinder_Dashboard/
├── lib/
│   ├── main.dart                      # 入口 + App 配置
│   ├── app/
│   │   ├── app.dart                   # MaterialApp 配置
│   │   ├── theme/
│   │   │   ├── app_colors.dart        # 色板（赛车暗色风格）
│   │   │   ├── app_theme.dart         # ThemeData (Material 3)
│   │   │   └── app_typography.dart    # 字体层级
│   │   └── router.dart                # 路由配置
│   │
│   ├── core/
│   │   ├── ble/
│   │   │   ├── ble_service.dart       # 连接管理 + GATT 读写
│   │   │   ├── ble_uuids.dart         # Service/Characteristic UUID 常量
│   │   │   ├── ble_codec.dart         # 数据帧编解码
│   │   │   └── mock_ble_service.dart  # 开发期间 Mock 数据源
│   │   └── storage/
│   │       ├── database.dart          # Drift 数据库定义
│   │       ├── dao_env.dart           # 环境数据 DAO
│   │       ├── dao_event.dart         # 运动事件 DAO
│   │       └── dao_emote.dart         # 表情记录 DAO
│   │
│   ├── features/
│   │   ├── connection/
│   │   │   ├── connection_screen.dart
│   │   │   ├── connection_controller.dart
│   │   │   └── widgets/
│   │   ├── environment/
│   │   │   ├── environment_screen.dart
│   │   │   ├── environment_controller.dart
│   │   │   └── widgets/
│   │   ├── motion/
│   │   │   ├── motion_screen.dart
│   │   │   ├── motion_controller.dart
│   │   │   └── widgets/
│   │   ├── emote/
│   │   │   ├── emote_screen.dart
│   │   │   ├── emote_controller.dart
│   │   │   └── widgets/
│   │   └── history/
│   │       ├── history_screen.dart
│   │       └── widgets/
│   │
│   └── shared/
│       ├── models/
│       │   ├── env_snapshot.dart
│       │   ├── imu_snapshot.dart
│       │   ├── motion_event.dart
│       │   ├── emote_info.dart
│       │   └── emote_rules.dart
│       ├── providers/
│       │   ├── ble_provider.dart
│       │   ├── sensor_provider.dart
│       │   └── emote_provider.dart
│       └── widgets/
│           ├── metric_card.dart
│           ├── status_indicator.dart
│           └── animated_counter.dart
│
├── assets/
│   ├── emotes/                        # 24 个表情动画 (WebP/GIF)
│   └── icons/
│
├── test/
│   ├── ble/
│   │   └── ble_codec_test.dart
│   ├── emote/
│   │   └── emote_rules_test.dart
│   ├── storage/
│   │   └── env_dao_test.dart
│   └── widgets/
│       └── metric_card_test.dart
│
├── pubspec.yaml
└── README.md
```

### 2.3 技术栈

| 层 | 库 | 版本约束 | 用途 |
|----|----|---------|------|
| 框架 | Flutter | >=3.22.0 | Android 应用框架 |
| 状态管理 | flutter_riverpod | ^2.5.0 | 编译时安全、流式数据 |
| BLE | flutter_reactive_ble | ^5.3.1 | 纯 Dart BLE 通信 |
| 图表 | fl_chart | ^0.68.0 | 折线图/波形图 |
| 数据库 | drift | ^2.18.0 | 类型安全 SQL ORM |
| SQLite | sqlite3_flutter_libs | ^0.5.24 | Drift 原生依赖 |
| 导航 | go_router | ^14.2.0 | 声明式路由 |
| 日志 | logger | ^2.4.0 | 分级日志 |

### 2.4 数据流架构

```
ESP32 传感器 ──BLE GATT Notify──> Flutter
                                     │
                          ┌──────────┴──────────┐
                          │  ble_codec (解码)     │
                          └──────────┬──────────┘
                                     │
                    ┌────────────────┼────────────────┐
                    ▼                ▼                 ▼
            sensor_provider    emote_provider    storage/DAO
            (实时流)           (表情状态)         (持久化)
                    │                │                 │
                    ▼                ▼                 │
              UI Widgets (fl_chart, 动画展示)    历史查询 ◄─┘
```

## 3. BLE 通信协议规范

### 3.1 GATT Service 结构

```
Service UUID: 0000fe00-0000-1000-8000-00805f9b34fb
├── C1 Device Info        (READ)               UUID: 0000fe01-... — 设备信息
├── C2 Environment Data   (NOTIFY @1Hz)        UUID: 0000fe02-... — 环境传感器
├── C3 Motion Data        (NOTIFY @25Hz)       UUID: 0000fe03-... — IMU 姿态+事件
├── C4 Emote State        (NOTIFY @on-change)  UUID: 0000fe04-... — 表情状态
└── C5 Raw IMU            (NOTIFY @可配置)      UUID: 0000fe05-... — 原始6轴波形

（完整 UUID 格式均为 `0000fe0X-0000-1000-8000-00805f9b34fb`）
```

### 3.2 C1 — Device Info（READ）

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | name_len | u8 | 设备名长度 |
| 1 | name | char[16] | "PathFinder-EMOTE" |
| 17 | fw_major | u8 | 固件主版本 |
| 18 | fw_minor | u8 | 固件次版本 |
| 19 | sensor_mask | u8 bitmask | bit0=AHT20, bit1=BMP280, bit2=MPU6050, bit3=UV |

### 3.3 C2 — Environment Data（NOTIFY @1Hz，20 字节）

| 偏移 | 字段 | 类型 | 单位 | 范围 |
|------|------|------|------|------|
| 0 | timestamp | u32 LE | ms | 毫秒时间戳 |
| 4 | temperature | s16 LE | 0.01°C | -3276.8 ~ +3276.7°C |
| 6 | humidity | u16 LE | 0.01% | 0 ~ 655.35% |
| 8 | pressure | u32 LE | Pa | 绝对气压 |
| 12 | altitude | s16 LE | 0.1m | -3276.8 ~ +3276.7m |
| 14 | uv_index | u16 LE | 0.01 | 0.00 ~ 655.35 |
| 16 | reserved | u8[4] | — | 预留扩展 |

**编码策略：** 定点数编码（值 × 100 存为整数），避免浮点字节序问题。20 字节 < 默认 MTU 23 字节有效载荷。

### 3.4 C3 — Motion Data（NOTIFY @25Hz，8 字节）

| 偏移 | 字段 | 类型 | 单位 | 说明 |
|------|------|------|------|------|
| 0 | pitch | s16 LE | 0.01° | -327.68 ~ +327.67° |
| 2 | roll | s16 LE | 0.01° | 同上 |
| 4 | accel_mag | u16 LE | 0.001g | 偏离 1g 的绝对值 |
| 6 | event | u8 | enum | motion_event_t 枚举值 (0~12) |
| 7 | event_conf | u8 | % | 事件置信度 0~100 |

### 3.5 C4 — Emote State（NOTIFY @on-change，15 字节）

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | emote_id | u8 | 动画序号 (0~23) |
| 1 | name_len | u8 | 名称长度 |
| 2 | friendly_name | char[12] | 友好名称 (如 "Panic") |
| 14 | trigger_reason | u8 | 触发原因码 (见下表) |

**触发原因码：**

| 码 | 原因 | 对应表情 |
|----|------|---------|
| 0 | MANUAL | 手动切换 |
| 1 | UV Extreme (>=8) | Panic |
| 2 | UV High (>=6) | Mock |
| 3 | Hot (>=32°C) | Sigh |
| 4 | Cold (<=10°C) | Sad |
| 5 | Humid (>=85%) | Cry |
| 6 | Tilted (>=20°) | Question |
| 7 | Slight Tilt (>=12°) | Investigate |
| 8 | Low Pressure (<=1000hPa) | Ponder |
| 9 | Normal | Leisure/Smile |

### 3.6 C5 — Raw IMU（NOTIFY @可配置，24 字节）

| 偏移 | 字段 | 类型 | 单位 | 说明 |
|------|------|------|------|------|
| 0 | timestamp | u16 LE | ms%60000 | 毫秒时间戳(模60s) |
| 2 | accel[3] | s16×3 LE | 0.001g | 加速度 XYZ |
| 8 | gyro[3] | s16×3 LE | 0.01°/s | 陀螺仪 XYZ |
| 14 | temp | s8 | °C | MPU6050 温度 |
| 15 | reserved | u8[9] | — | 预留 |

**配置机制（向 C5 写入 1 字节）：** 0=关闭（默认）, 1=10Hz, 2=25Hz, 3=50Hz。

### 3.7 ESP32 固件改造清单（后续独立任务）

1. 新增 `ble_service.c/h` — NimBLE 协议栈 + GATT 服务注册
2. 修改 `sensor_manager.c` — 采样完成后调用 `ble_notify_env()` / `ble_notify_motion()`
3. 修改 `emote_engine.c` — 在 `play_emote()` 中调用 `ble_notify_emote()`
4. 新增 `ble_codec.c/h` — 结构体到字节数组序列化

## 4. UI 设计

### 4.1 设计语言

延续 ESP32 圆形屏赛车暗色风格，Material 3 暗色适配。

| 设计令牌 | 值 | 用途 |
|---------|---|------|
| background | `#0A0A0F` | 纯黑底 |
| surface | `#1A1A24` | 卡片背景 |
| envPrimary | `#00B4FF` | 环境数据(青) |
| motionPrimary | `#00FF88` | 运动数据(绿) |
| warning | `#FFB400` | UV/倾角警告(黄) |
| urgent | `#FF5050` | 碰撞/急刹车(红) |
| textPrimary | `#E0E0E8` | 正文 |
| textSecondary | `#888899` | 标签/注释 |

### 4.2 导航结构

BottomNavigationBar 4 Tab + IndexedStack 保持状态：
- Tab 0 设备 (Icons.bluetooth)
- Tab 1 环境 (Icons.thermostat)
- Tab 2 运动 (Icons.sports_motorsports)
- Tab 3 表情 (Icons.face)

未连接设备时 Tab 1/2/3 显示「等待连接」占位卡片。

### 4.3 Tab 0 — 设备连接页

- BLE 扫描动画 + 发现设备列表 (ListView.builder + DeviceTile)
- 每个设备显示名称、RSSI、固件版本
- 连接成功后 AnimatedSwitcher 切换到详情卡片（传感器在线状态、连接时长、MTU、延迟）
- 断开连接按钮

### 4.4 Tab 1 — 环境数据页

- 5 个 MetricCard（温度、湿度、气压、海拔、UV），AnimatedCounter 数字滚动
- fl_chart LineChart 双线实时折线图（最近60秒），双Y轴温度+湿度
- 可切换查看气压/UV趋势
- 卡片点击 BottomSheet 展开单指标详细图表
- 异常状态警告条（UV偏高/温度异常时显示）
- 「历史」入口跳转 HistoryScreen

**图表性能：** 60秒滑动窗口(60点)，`swapAnimationDuration: 300ms`，`isCurved: true`。

### 4.5 Tab 2 — 运动数据页

- AttitudeIndicator（CustomPaint 自定义水平仪：圆盘+气泡）
- Pitch/Roll 数值显示
- ImuWaveChart（fl_chart 三线叠加，X红 Y绿 Z蓝），250点滑动窗口(10秒@25Hz)
- 加速度波形 + 陀螺仪波形（可切换）
- EventTimeline（ListView.builder），从 Drift 查询事件记录
- 事件颜色：碰撞=红，急刹车=橙，转弯=黄，正常=灰
- 进入 Tab 时自动开启 C5 Raw IMU notify，离开时关闭

**性能关键：** StreamBuilder + RingBuffer(250)，RepaintBoundary 隔离重绘。

### 4.6 Tab 3 — 表情状态页

- CurrentEmoteCard（Image.asset 播放当前表情动画 + AnimatedSwitcher 缩放淡入切换）
- 友好名称大字 + 触发原因文字（如「紫外线极端危险」）
- EmoteMappingTable（ListView）— 9 条映射规则，当前命中行主色高亮+缩放动画
- EmoteGallery（GridView.builder 6列）— 24 种动画横向网格（22种有友好名称映射，2种显示原始名），点击预览 Dialog
- EmoteHistory — 从 Drift 查询，显示表情切换时间线 + 触发时传感器数值

### 4.7 历史页（共享）

- 时间范围选择：今天/昨天/7天/自定义
- 概要统计：最高/最低温度、最大UV、事件总数
- 全天数据折线图
- 导出 CSV / 分享图表

### 4.8 动画策略

| 场景 | 类型 | 时长 | 实现 |
|------|------|------|------|
| 数字变化 | 滚动计数 | 300ms | TweenAnimationBuilder<double> |
| 卡片切换 | 淡入淡出 | 200ms | AnimatedSwitcher |
| 表情切换 | 缩放+淡入 | 400ms | AnimatedSwitcher + ScaleTransition |
| 连接状态变化 | 脉冲发光 | 循环 | AnimationController + BoxShadow |
| 异常告警 | 闪烁脉冲 | 1000ms | CustomPaint + RepaintBoundary |
| 图表数据更新 | 线条绘制 | 300ms | fl_chart 内置 |

## 5. 数据模型与存储

### 5.1 Dart 数据模型

所有模型 1:1 对齐 ESP32 C 结构体，提供 `fromBle()` 工厂方法从 BLE 字节解码。

- **EnvSnapshot** — 温度/湿度/气压/海拔/UV，对应 C2 (20B)
- **ImuSnapshot** — pitch/roll/accelMag/event/confidence，对应 C3 (8B)
- **RawImuFrame** — 6轴原始+温度，对应 C5 (24B)
- **EmoteInfo** — emoteId/friendlyName/trigger，对应 C4 (15B)
- **MotionEvent** — 12种事件枚举 (code/label/color)
- **EmoteTrigger** — 10种触发原因枚举 (code/label)
- **EmoteRule** — 9条映射规则（Dart 复刻 evaluate_sensors 逻辑）

### 5.2 表情映射规则表（复刻 ESP32 evaluate_sensors）

| 优先级 | 表情 | 触发条件 | 显示文字 |
|--------|------|---------|---------|
| 90 | panic | UV >= 8.0 | 紫外线极端危险 |
| 80 | mock | UV >= 6.0 | 紫外线较强 |
| 70 | sigh | 温度 >= 32°C | 温度过高 |
| 65 | sad | 温度 <= 10°C | 温度过低 |
| 60 | cry | 湿度 >= 85% | 高湿度不适 |
| 50 | question | 倾角 >= 20° | 检测到严重倾斜 |
| 40 | investigate | 倾角 >= 12° | 检测到倾斜 |
| 30 | ponder | 气压 <= 1000hPa | 气压偏低 |
| 10 | leisure | 默认 | 一切正常 |

### 5.3 Drift 数据库（3 表 + schemaVersion 1）

**env_records：** id, timestamp, temperature, humidity, pressure, altitude, uv_index

**event_records：** id, timestamp, eventType, pitch, roll, accelMag, duration

**emote_records：** id, timestamp, emoteId, friendlyName, triggerCode, triggerLabel, sensorValue

**注意：** 原始 IMU 6轴数据(@25Hz)不入库，仅内存 RingBuffer(250) 用于实时波形。一天约 50MB，成本过高。

### 5.4 存储容量估算

| 数据类型 | 频率 | 日数据量 |
|---------|------|---------|
| 环境记录 | 86,400/天 | ~3.4MB |
| 事件记录 | ~200/天 | ~10KB |
| 表情记录 | ~100/天 | ~6KB |
| 合计 | — | ~3.5MB/天 |

提供「自动清理 30 天前数据」设置项。30 天约 105MB。

### 5.5 采样与降频策略

| 数据流 | BLE 频率 | 入库频率 | 内存缓冲 |
|--------|---------|---------|---------|
| 环境数据 | 1Hz | 1Hz（全量） | 60点 |
| 运动事件 | 25Hz（仅状态） | on-change | 无 |
| 表情状态 | on-change | on-change | 无 |
| 原始 IMU | 可配置 | 不入库 | 250点(10秒) |

### 5.6 Riverpod Provider 架构

```
bleServiceProvider (BleService)
    │
    ├── connectionStateProvider (StreamProvider<ConnectionState>)
    ├── envStreamProvider (StreamProvider<EnvSnapshot>) → envDaoProvider
    ├── imuStreamProvider (StreamProvider<ImuSnapshot>) → eventDaoProvider
    ├── rawImuProvider (StreamProvider<RawImuFrame>) → RingBuffer
    └── emoteStreamProvider (StreamProvider<EmoteInfo>) → emoteDaoProvider
         │
         ├── envChartProvider (StateProvider<List<EnvSnapshot>>)
         └── imuChartProvider (StateProvider<List<RawImuFrame>>)
```

## 6. 错误处理

### 6.1 BLE 连接错误

| 场景 | 处理策略 | 用户感知 |
|------|---------|---------|
| 蓝牙未开启 | 检测适配器状态，引导开启 | 弹窗「请开启蓝牙」 |
| 设备未找到 | 10秒超时，提示重试 | 「未找到设备」+重试按钮 |
| 连接断开 | 监听 connectionStream，自动重连3次(2/4/8秒退避) | 顶部红色「重连中...」 |
| GATT 通信失败 | 标记数据陈旧 | 卡片灰色「--」占位 |
| MTU 协商失败 | 回退默认23字节 | 无感知（协议已适配） |
| Notify 校验失败 | 长度不符丢弃+日志 | 无感知 |
| RSSI < -90dBm | 提示信号弱 | 顶部黄色警告条 |

### 6.2 UI 状态容错

使用 Riverpod `AsyncValue.when()`：data 正常渲染 / loading 骨架屏 / error 判断连接状态显示错误卡片或未连接占位。

### 6.3 数据库错误

- 打开失败：回退仅实时模式，通知用户
- 写入失败：静默丢弃（每秒刷新，单条可接受）
- 迁移失败：清库重建（schemaVersion=1 无升级路径）

## 7. 测试策略

### 7.1 测试覆盖优先级

| 优先级 | 范围 | 方式 |
|--------|------|------|
| P0 | BLE 协议编解码（全字段+边界值） | 单元测试 |
| P0 | 表情映射规则（9条全部） | 单元测试 |
| P1 | DAO 写入/查询/聚合 | 集成测试（内存 SQLite） |
| P1 | 关键 Widget 渲染 | Widget 测试 |
| P2 | BLE 连接流程（扫描→连接→断开→重连） | Mock 集成测试 |
| P2 | UI 状态容错（未连接/错误占位） | Widget 测试 |

### 7.2 Mock 数据源

`MockBleService` 实现 `BleServiceInterface`，生成模拟传感器数据（正弦波+随机扰动），通过 Riverpod `override` 切换，无需 ESP32 硬件即可开发调试 UI。

## 8. 交付里程碑

| 阶段 | 交付物 | 验收标准 |
|------|--------|---------|
| M1 | 项目骨架 + 主题 + 导航 + Mock数据 | 4 Tab 可切换，Mock 数据驱动 UI |
| M2 | BLE 通信层 + 协议编解码 | 真机扫描连接 ESP32（需固件配合） |
| M3 | 环境 Tab + 运动 Tab 完整 UI | fl_chart 实时图表 + 波形图 |
| M4 | 表情 Tab + 历史页 | 动画展示 + 映射表 + 历史查询 |
| M5 | 存储 + 导出 + 优化 | Drift 入库 + CSV 导出 + 性能调优 |

## 9. 不在本次范围内

- ESP32 固件 BLE GATT 服务实现（后续独立任务，规范见 Section 3）
- iOS / macOS / Windows 平台支持
- 云端数据同步
- 多设备同时连接
- OTA 固件升级
