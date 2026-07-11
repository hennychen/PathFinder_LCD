---
kind: error_handling
name: ESP-IDF 风格错误处理：ESP_ERROR_CHECK + esp_err_t 返回值
category: error_handling
scope:
    - '**'
source_files:
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/rgb_lcd_example_main.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/wifi/esp32wifi.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/led_ws2812/led_ws2812.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/LCD.c
---

本仓库基于 ESP-IDF，采用其统一的错误处理约定：所有可能失败的 API 返回 esp_err_t，调用方通过宏 ESP_ERROR_CHECK() 在初始化阶段快速失败（fail-fast），并在关键路径上配合 assert()、ESP_LOGE 与事件回调中的降级逻辑。

1. 系统/框架
- 错误码类型：esp_err_t（来自 esp_err.h）。
- 快速失败宏：ESP_ERROR_CHECK(ret)，失败时打印日志并进入异常路径。
- 资源分配断言：assert(ptr) 用于驱动内部必须成功的内存分配。
- 日志：ESP_LOGI/ESP_LOGE(TAG, ...) 输出结构化日志。
- 事件回调：Wi-Fi 使用 esp_event_loop 注册回调，在断开等场景做重连而非向上抛错。

2. 关键文件与位置
- main/rgb_lcd_example_main.c：应用入口，大量 ESP_ERROR_CHECK 包装 LCD/RMT/timer 初始化；LVGL 任务内用递归互斥锁保护。
- main/wifi/esp32wifi.c：NVS/Wi-Fi 初始化链式 ESP_ERROR_CHECK；get_wifi_signal_strength 对 esp_wifi_sta_get_ap_info 的失败走 ESP_LOGE + 回退值。
- main/led_ws2812/led_ws2812.c：自定义 RMT 编码器内部用 ESP_GOTO_ON_FALSE + goto err 清理资源；对外接口返回 esp_err_t，调用处统一 ESP_ERROR_CHECK。
- main/LCD.c：背光 GPIO 配置同样以 ESP_ERROR_CHECK 包裹。

3. 架构与约定
- 初始化阶段：全部使用 ESP_ERROR_CHECK，一旦失败立即中止启动，避免半初始化状态继续运行。
- 运行时可恢复错误：仅在少数查询类函数中检查返回值并记录 ESP_LOGE，返回默认值或 -100 等安全回退。
- 资源释放：在 rmt_new_led_strip_encoder 的错误分支里集中释放已分配的 encoder 和内存，体现失败即清理的约定。
- 无 panic/recover 机制：未使用 C++ 异常或 __attribute__((cleanup)) 等高级手段，保持嵌入式简洁风格。

4. 开发者应遵循的规则
- 所有返回 esp_err_t 的 SDK 调用，除非明确需要异步/重试，否则一律用 ESP_ERROR_CHECK(...) 包裹。
- 内存分配后紧跟 assert(ptr)，确保致命 OOM 能立刻暴露。
- 需要在运行时容忍的错误才显式判断 ret == ESP_OK，并配合 ESP_LOGE(TAG, "...") 记录上下文。
- 不要在 LVGL 刷新回调或中断上下文中执行阻塞错误处理；同步 LVGL 访问已通过递归互斥锁 example_lvgl_lock/unlock 保护。
- 新增模块若提供对外 API，请返回 esp_err_t，并在内部失败路径中释放已分配资源，参考 rmt_new_led_strip_encoder 的 err 标签模式。