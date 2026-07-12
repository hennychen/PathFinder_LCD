# ESP32 Wi-Fi 双模配网设计

> **日期**: 2026-07-12
> **状态**: Approved
> **范围**: PathFinder_EMOTE 固件 + PathFinder_Dashboard Flutter App

## 1. 概述

为 PathFinder EMOTE 设备增加 Wi-Fi 配网能力，支持两种配网路径：

- **BLE 配网** — 通过 PathFinder Dashboard App 蓝牙连接后发送 Wi-Fi 凭据
- **Web 配网** — ESP32 开启 AP 热点，手机连接后通过 Captive Portal 网页配置

两种路径共享统一的 `wifi_config_manager` 模块，凭据持久化到 NVS，配网成功后 AP + HTTP Server 立即销毁释放内存。

### 1.1 设计目标

- 首次开机无凭据时自动进入配网模式（BLE + Web 双开）
- 已有凭据时自动 STA 连接，连续失败 5 次重进配网模式
- Web Portal 为瞬态组件，配网后全部回收内存，不影响未来 xiaozhi AI 等高内存功能
- LCD 屏幕显示完整配网状态（4 状态），复用现有 EMA 胶囊风格
- Flutter App 新增 Wi-Fi 设置入口

### 1.2 非目标

- 不实现 OTA 固件更新（后续）
- 不实现 MQTT 数据上传（后续）
- 不实现 NTP 时间同步（后续）
- 不实现触摸屏屏幕键盘输入 Wi-Fi 密码

## 2. 系统架构

### 2.1 状态机

```
开机 BOOT
    │
    ▼
┌─────────────────┐     有凭据      ┌──────────────────────┐
│ 读取 NVS 凭据？  │───────────────▶│  STA 模式             │
└─────────────────┘                 │  · Wi-Fi STA 连接路由器│
    │ 无凭据                        │  · BLE 广播 (传感器数据)│
    ▼                               │  · 正常 EAF + 胶囊 UI  │
┌──────────────────────┐           └──────────┬───────────┘
│ 配网模式 (双开)        │                      │
│ · BLE 广播 + Write 特征│                      │ 连续失败 5 次
│ · Wi-Fi AP 热点        │◀─────────────────────┘
│ · HTTP Server + Portal │
│ · LCD 配网状态页       │
└──────────┬───────────┘
           │ 收到凭据
           ▼
┌──────────────────────┐  成功   ┌──────────────────────┐
│ CONNECTING           │───────▶│ NVS 保存 → STA 模式   │
│ · STA 尝试连接        │        │ · AP + HTTP 销毁      │
│ · 屏幕显示进度         │        │ · 屏幕恢复正常 UI     │
└──────────┬───────────┘  失败   └──────────────────────┘
           │──────────────────────▶ 返回配网模式
           ▼
```

### 2.2 模块架构

```
main.c (app_main)
  │
  ├── wifi_config_manager ← 核心，统一管理
  │     ├── NVS 存取 SSID/Password
  │     ├── STA/AP 模式切换
  │     ├── 重连逻辑 (失败 5 次)
  │     └── 状态回调 (通知 UI + BLE)
  │
  ├── web_portal
  │     ├── HTTP Server (esp_http_server)
  │     ├── Captive Portal (302 重定向)
  │     ├── Wi-Fi 扫描 API (esp_wifi_scan)
  │     └── 配网页面 HTML (Flash RODATA)
  │
  ├── provision_screen (LVGL)
  │     ├── 4 状态覆盖层 (WAITING/CONNECTING/CONNECTED/FAILED)
  │     └── 复用 EMA 胶囊风格 + 动画
  │
  └── ble_gatt_server (已有，改造)
        ├── C2/C3/C4 notify (传感器数据，不变)
        └── C5 Write+Notify (新增，配网命令)
              │
              ▼
        wifi_config_manager (JSON 命令回调)
```

### 2.3 启动流程改造

`app_main()` 初始化顺序调整：

```c
// 现有初始化 (不变)
Lcd_Initialize();           // LCD 硬件
// RGB 面板配置
lv_init();                  // LVGL
touch_init();               // 触摸
app_emote_assets_init();    // EAF 资源
sensor_i2c_init();          // 传感器 I2C
motion_engine_init();       // 运动引擎
ui_create(disp);            // UI 创建

// 新增：Wi-Fi 配网初始化
wifi_config_manager_init(); // 读取 NVS，决定进入 STA 还是配网模式

// 现有初始化 (不变)
sensor_manager_init();
emote_engine_init();
ble_gatt_server_init();     // BLE (C5 特征值已包含)

// 启动任务
xTaskCreate(lvgl_task, ...);
xTaskCreate(ble_notify_task, ...);
```

## 3. BLE 配网协议

### 3.1 新增特征值

在现有 Service `0xFE00` 下新增第 5 个特征值：

| 特征值 | UUID | 属性 | 用途 |
|--------|------|------|------|
| C2 Env | 0xFE02 | Read + Notify | 环境数据 (已有) |
| C3 Motion | 0xFE03 | Read + Notify | 运动数据 (已有) |
| C4 Emote | 0xFE04 | Read + Notify | 表情状态 (已有) |
| **C5 WiFi** | **0xFE05** | **Write + Notify** | **配网命令 + 状态回传** |

### 3.2 JSON 命令格式

**App → ESP32 (Write 到 0xFE05)：**

```json
{"cmd":"set_wifi","ssid":"MyHome","pass":"12345678"}
{"cmd":"get_status"}
{"cmd":"reset_wifi"}
{"cmd":"scan"}
```

**ESP32 → App (Notify 从 0xFE05)：**

```json
{"status":"provisioning","msg":"waiting for credentials"}
{"status":"connecting","ssid":"MyHome"}
{"status":"connected","ssid":"MyHome","ip":"192.168.1.100"}
{"status":"failed","ssid":"MyHome","error":"auth_failed"}
{"scan_result":[{"ssid":"Home","rssi":-55,"auth":3},{"ssid":"Office","rssi":-72,"auth":3}]}
```

### 3.3 分包处理

BLE 单次 Write 最大 payload 约 180 bytes (ATT MTU 限制)。JSON 可能超长，采用简单的分包协议：

- Write payload 第一个字节为分片标志：`0x00` = 完整帧，`0x01` = 分片后续
- 收到 `0x00` 开头时，若 JSON 不完整，缓冲等待下一帧
- 收到完整 JSON (闭合 `}`) 后解析执行
- 最大缓冲 512 bytes，超出则丢弃重来

### 3.4 UUID 注册约束

沿用现有 NimBLE 16-bit UUID 注册方式，必须使用 `BLE_UUID16_INIT`：

```c
static const ble_uuid16_t c5_wifi_uuid = BLE_UUID16_INIT(0xFE05);
```

## 4. Web Captive Portal

### 4.1 技术实现

| 组件 | ESP-IDF API | 说明 |
|------|-------------|------|
| Wi-Fi AP | `esp_wifi_set_mode(WIFI_MODE_APSTA)` | APSTA 模式：AP 供手机连接 + STA 可同时扫描 |
| HTTP Server | `esp_http_server` | 轻量级，~8KB 栈，配网后 `httpd_stop()` 销毁 |
| Captive Portal | DNS 拦截 + 302 重定向 | 所有 HTTP 请求重定向到配网页 |
| Wi-Fi 扫描 | `esp_wifi_scan_start()` | APSTA 模式下 AP 频道扫描周边 Wi-Fi |

### 4.2 AP 热点配置

- SSID: `PathFinder-EMOTE` (固定名称)
- 密码: 无 (开放热点，方便连接)
- 频道: 1 (默认)
- 最大连接数: 1 (仅需手机连接)

### 4.3 HTTP 端点

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/` | 返回配网主页 HTML (Flash RODATA) |
| GET | `/api/scan` | 返回 JSON 格式 Wi-Fi 列表 |
| POST | `/api/connect` | 接收 `{ssid, password}` → 保存 → 尝试连接 |
| GET | `/api/status` | 返回连接状态 (idle/connecting/connected/failed) |
| GET | `/generate_204` | Android Captive Portal 探测 → 302 重定向 |
| GET | `/hotspot-detect.html` | iOS Captive Portal 探测 → 302 重定向 |

### 4.4 配网页面 HTML

HTML 以 `const char[]` 存储在 Flash RODATA 段，不占用 RAM。页面包含：

- Wi-Fi 列表 (通过 `/api/scan` 获取，按 RSSI 排序)
- 信号强度图标
- 密码输入框
- 连接按钮
- 状态轮询 (`/api/status` 每 500ms)

### 4.5 Captive Portal 拦截

- **Android** — HTTP 204 探测 `/generate_204` 返回 302 重定向到 `/`
- **iOS** — 拦截 `/hotspot-detect.html` 返回 302 重定向到 `/`
- **通用** — 所有未知路径返回配网页 HTML

## 5. 内存预算分析

### 5.1 配网模式 (临时，约 30 秒~2 分钟)

**内部 SRAM (512KB) 占用：**

| 组件 | 占用 |
|------|------|
| IRAM/IDLE (系统) | ~180KB |
| Bounce Buffer ×2 (LCD) | ~76KB |
| BLE NimBLE | ~40KB |
| **Wi-Fi AP 栈** | **~25KB** |
| **HTTP Server 栈** | **~8KB** |
| LVGL + 传感器任务 | ~60KB |
| **剩余可用** | **~123KB** |

**PSRAM (8MB)：** LCD 帧缓冲 + LVGL 占 ~1.1MB，剩余 ~6.9MB

### 5.2 正常 STA 模式 (配网完成后)

AP + HTTP Server 全部销毁，回收 ~33KB 内部 RAM。
剩余内部 SRAM ~156KB，PSRAM ~6.9MB。

### 5.3 未来 xiaozhi AI 接入

在 STA 模式基础上叠加：

| AI 组件 | 内部 SRAM | PSRAM |
|---------|-----------|-------|
| I2S 音频环形缓冲 | ~24KB | — |
| OPUS 编解码 | ~48KB | — |
| WebSocket/MQTT | ~20KB | — |
| TTS 音频缓冲 | — | ~512KB |
| ASR 流式缓冲 | — | ~256KB |

**结论：** 内部 SRAM 仍剩 ~64KB，PSRAM 剩 ~5MB。Web Portal 的瞬态设计不会造成内存瓶颈。

### 5.4 HTTP Server 内存优化配置

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.stack_size = 4096;           // 最小栈
config.max_uri_handlers = 6;        // 仅 6 个端点
config.lru_purge_enable = true;     // 自动清理旧连接
config.recv_wait_timeout = 5;       // 快速超时回收
```

## 6. LVGL 配网屏幕

### 6.1 四种状态

| 状态 | 屏幕显示 | 持续时间 |
|------|---------|---------|
| **WAITING** | 黄色边框 (#FFB400)，显示 AP 热点名 `PathFinder-EMOTE` + BLE/Web 胶囊标签 + 底部脉冲点 + IP `192.168.4.1` | 直到收到凭据 |
| **CONNECTING** | 蓝色边框 (#00B4FF)，显示目标 SSID + 进度条 + 尝试次数 `1/5` | STA 连接尝试中 |
| **CONNECTED** | 绿色边框 (#00FF88)，✅ 图标 + SSID + IP 地址 | 2 秒后淡出进入正常 UI |
| **FAILED** | 红色边框 (#FF5050)，❌ 图标 + 错误原因 | 3 秒后返回 WAITING |

### 6.2 实现接口

```c
// 创建配网覆盖层 (类似现有 detail_page_create 模式)
void provision_screen_create(lv_obj_t *parent);

// 切换显示状态
void provision_screen_set_state(provision_state_t state, const char *ssid, const char *detail);

// 销毁覆盖层，恢复正常 UI
void provision_screen_destroy(void);
```

### 6.3 样式复用

- 颜色复用 EMA 胶囊色系 (#00B4FF 蓝 / #00FF88 绿 / #FFB400 黄 / #FF5050 红)
- 胶囊标签复用 `create_capsule()` 圆角样式
- 字体：montserrat_16 / montserrat_20
- 背景纯黑 #000
- 动画：300ms 淡入淡出状态切换

### 6.4 与正常 UI 的关系

配网覆盖层创建在 `lv_scr_act()` 上，覆盖 EAF 表情和胶囊。配网完成后调用 `provision_screen_destroy()` 移除覆盖层，EAF 表情 UI 重新可见。

所有 LVGL 操作通过现有 `lvgl_lock/unlock` 互斥锁保护。

## 7. 文件变更总览

### 7.1 ESP32 固件侧 (PathFinder_EMOTE/main/)

**新增文件：**

| 文件 | 职责 |
|------|------|
| `wifi_config_manager.c/h` | NVS 存取, STA/AP 切换, 重连逻辑, 状态回调 |
| `web_portal.c/h` | HTTP Server, Captive Portal, Wi-Fi 扫描 API |
| `provision_screen.c/h` | LVGL 配网 UI 覆盖层 (4 状态) |

**修改文件：**

| 文件 | 变更 |
|------|------|
| `main.c` | `app_main()` 加入 `wifi_config_manager_init()`，启动流程改造 |
| `ble_gatt_server.c/h` | 新增 C5 (0xFE05) Write+Notify 特征值，JSON 解析回调 |
| `CMakeLists.txt` | SRCS 加 3 个新 .c，REQUIRES 加 `esp_wifi` `esp_http_server` `esp_event` |
| `sdkconfig.defaults` | 加 `CONFIG_ESP_WIFI_ENABLED=y` 等 Wi-Fi 支持 |

### 7.2 Flutter App 侧 (PathFinder_Dashboard/lib/)

**新增文件：**

| 文件 | 职责 |
|------|------|
| `features/wifi/wifi_setup_screen.dart` | Wi-Fi 配网页面 |
| `features/wifi/widgets/wifi_scan_list.dart` | 扫描列表组件 |
| `core/ble/ble_wifi_writer.dart` | BLE C5 Write 封装 |

**修改文件：**

| 文件 | 变更 |
|------|------|
| `core/ble/ble_uuids.dart` | 加 0xFE05 特征值 UUID |
| `core/ble/reactive_ble_service.dart` | 加 `writeWifiConfig()` 方法 |
| `app/app.dart` | 加 Wi-Fi 设置入口路由 |

## 8. sdkconfig.defaults 变更

新增以下配置项：

```ini
# Wi-Fi
CONFIG_ESP_WIFI_ENABLED=y
CONFIG_ESP_WIFI_NVS_ENABLED=y

# HTTP Server (用于 Web Portal)
CONFIG_HTTPD_MAX_REQ_HDR_LEN=512
CONFIG_HTTPD_MAX_URI_LEN=512
```

## 9. 错误处理

| 场景 | 处理 |
|------|------|
| NVS 读取失败 | 视为无凭据，进入配网模式 |
| Wi-Fi STA 连接超时 (10 秒/次) | 重试，5 次后重进配网模式 |
| Wi-Fi STA 认证失败 | 立即重进配网模式 (不浪费重试次数) |
| HTTP Server 启动失败 | 仅依赖 BLE 配网，LCD 显示 BLE 标签 |
| BLE Write JSON 解析失败 | Notify 返回 `{"status":"failed","error":"invalid_json"}` |
| 分包超时 (3 秒未收齐) | 丢弃缓冲，等待重新发送 |

## 10. 测试计划

| 测试项 | 方法 |
|--------|------|
| 首次开机进入配网模式 | 清除 NVS，重启，验证 AP 热点出现 + LCD 显示配网页 |
| BLE 配网完整流程 | App 连接 → 发送 set_wifi → 验证 STA 连接 + LCD 切换 |
| Web Portal 配网完整流程 | 手机连热点 → 弹出网页 → 选 Wi-Fi 输密码 → 验证连接 |
| 已有凭据自动连接 | 正常重启 → 验证直接进入 STA 模式 |
| 路由器离线重连 | 关闭路由器 → 验证 5 次失败后重进配网模式 |
| 手动重置 Wi-Fi | App 发送 reset_wifi → 验证清除 NVS + 重进配网 |
| 配网后内存回收 | 连接成功后 `esp_get_free_heap_size()` 验证 AP+HTTP 已释放 |
