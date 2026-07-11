---
kind: configuration_system
name: ESP-IDF Kconfig + sdkconfig.defaults 构建期配置体系
category: configuration_system
scope:
    - '**'
source_files:
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/sdkconfig.defaults
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/sdkconfig.defaults.esp32s3
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/Kconfig.projbuild
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/idf_component.yml
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/CMakeLists.txt
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/wifi/esp32wifi.h
---

本仓库是一个基于 ESP-IDF 的嵌入式固件工程，其“运行时配置”完全由 ESP-IDF 的构建期配置系统承担，没有独立的运行时配置文件加载逻辑。配置来源与分层如下：

1. 默认配置层（sdkconfig.defaults）
   - `sdkconfig.defaults`：为所有目标提供 LVGL 相关默认开关（如 `CONFIG_LV_MEM_CUSTOM`、`CONFIG_LV_USE_CHART` 等），是全局默认值。
   - `sdkconfig.defaults.esp32s3`：针对 ESP32-S3 芯片的额外默认项（PSRAM、EDMA 取指/只读数据优化等）。
   - CI 专用 `sdkconfig.ci.*` 文件用于不同帧缓冲策略的自动化测试。

2. 项目级 Kconfig 菜单（main/Kconfig.projbuild）
   - 通过 `menu "Example Configuration"` 暴露三个布尔选项：`EXAMPLE_DOUBLE_FB`、`EXAMPLE_USE_BOUNCE_BUFFER`、`EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM`，并在 help 中说明双缓冲与防撕裂取舍。这些选项在 `make menuconfig` / `idf.py menuconfig` 时可见并可修改。

3. 组件依赖与版本约束（main/idf_component.yml）
   - 声明对 `idf: ">=4.4"` 和 `lvgl/lvgl: "~8.3.0"` 的依赖，配合根目录 `CMakeLists.txt` 中的 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` 完成组件解析。

4. 编译期宏注入（sdkconfig.h → C 代码）
   - 所有 Kconfig 生成的 `CONFIG_*` 宏通过 `#include "sdkconfig.h"` 进入源码。例如 `LCD.c` 直接 include `sdkconfig.h`；LVGL 的 `lv_conf_internal.h` 也会根据 Kconfig 生成内部配置头。

5. 硬编码常量作为“不可变配置”
   - Wi-Fi SSID/密码以 `#define DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASSWORD` 形式写在 `main/wifi/esp32wifi.h` 中，属于编译期常量，不在运行时从 NVS/文件系统读取。
   - LCD 初始化寄存器表 `lcm_initialization_setting[]` 同样以静态数组硬编码在 `LCD.c` 中。

设计决策与约定
- 配置即构建产物：所有可配置项都通过 Kconfig/sdkconfig 在编译期固化到二进制，不存在运行时解析 JSON/YAML/env 的行为。
- 多目标差异化：用 `sdkconfig.defaults.<chip>` 按 SoC 拆分默认值，避免在 Kconfig 中写大量 `depends on SOC_ESP32S3`。
- 演示工程风格：Wi-Fi 凭据、屏驱动参数全部硬编码，未引入 NVS 持久化或 OTA 配置更新机制。

开发者应遵循的规则
- 新增可配置项优先使用 `Kconfig.projbuild` 定义，并通过 `sdkconfig.defaults` 给出合理默认值。
- 涉及不同芯片的差异默认值，新建 `sdkconfig.defaults.<soc>` 文件，不要混入通用 defaults。
- 需要跨模块共享的配置宏，统一通过 `sdkconfig.h` 暴露，不要在多个源文件中重复 `#define`。
- 若需运行时可改的配置（如 Wi-Fi 凭据），应在现有 Kconfig 基础上增加 NVS 读写逻辑，而不是继续用 `#define` 硬编码。