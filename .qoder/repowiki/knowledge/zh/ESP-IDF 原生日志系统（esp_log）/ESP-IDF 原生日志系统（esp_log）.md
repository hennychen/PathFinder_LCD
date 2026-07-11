---
kind: logging_system
name: ESP-IDF 原生日志系统（esp_log）
category: logging_system
scope:
    - '**'
source_files:
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/rgb_lcd_example_main.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/LCD.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/wifi/esp32wifi.c
---

本仓库基于 ESP-IDF 构建，应用层统一使用 ESP-IDF 内置的 esp_log.h 作为日志框架，未引入第三方日志库或自定义封装。所有业务模块通过 ESP_LOGI/ESP_LOGE 等宏输出结构化日志，由 IDF 运行时按组件名（TAG）路由到串口控制台。

- 使用的框架与头文件：#include "esp_log.h"，配合 sdkconfig.h 中的日志级别配置生效。
- 日志级别：代码中主要使用 ESP_LOGI（信息）和 ESP_LOGE（错误），未见 ESP_LOGD/WARN/TRACE 的使用；LVGL 组件内部也通过 LV_LOG_* 宏输出日志，但默认关闭（LV_LOG_LEVEL=LV_LOG_LEVEL_WARN，LV_LOG_PRINTF=0）。
- TAG 约定：每个源文件定义 static const char *TAG = "..."; 作为组件标签，例如 main 入口用 example、WiFi 模块用 wifi、LCD 初始化处硬编码了字符串 "example"（不一致点）。
- 输出目标：依赖 IDF 默认的 UART 控制台 sink，无自定义 sink 或重定向逻辑。
- 结构化字段：当前日志为纯文本消息，未附加 JSON 或键值对字段；若需结构化，可在后续通过 esp_log_set_vprintf 或自定义 vprintf 实现。
- 与 LVGL 日志的关系：LVGL 的日志独立于 esp_log，通过 lv_conf_cmsis.h 中的 LV_LOG_LEVEL、LV_LOG_TRACE_* 控制，且默认不打印到 printf，因此 LVGL 内部调试日志在本工程中基本不可见。

开发者应遵循的规则：
1. 在需要日志的文件顶部定义 static const char *TAG = "模块名";，并统一使用 ESP_LOGI/E 记录关键流程与错误。
2. 避免在高频路径中使用 ESP_LOGD/TRACE，以免占用 CPU 和串口带宽。
3. 如需区分不同子系统，请为每个 .c/.h 配对设置独立的 TAG，便于过滤。
4. 不要直接调用 printf 替代 ESP_LOG*，以保证日志级别控制和未来 sink 切换能力。
5. 若需要结构化日志或远程收集，应在工程启动阶段通过 IDF 提供的 vprintf hook 集中处理，而非在各模块散改。