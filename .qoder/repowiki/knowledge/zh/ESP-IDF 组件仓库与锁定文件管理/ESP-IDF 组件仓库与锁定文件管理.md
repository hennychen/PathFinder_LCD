---
kind: dependency_management
name: ESP-IDF 组件仓库与锁定文件管理
category: dependency_management
scope:
    - '**'
source_files:
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/idf_component.yml
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/dependencies.lock
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/CMakeLists.txt
---

本仓库采用 ESP-IDF 官方组件管理系统（idf_component_manager）进行第三方依赖声明与版本锁定，核心机制如下：

1. **依赖声明**：在 `main/idf_component.yml` 中以 YAML 形式声明直接依赖，当前仅声明了 `idf: ">=4.4"` 和 `lvgl/lvgl: "~8.3.0"`。
2. **版本锁定**：构建时生成 `dependencies.lock`，记录每个组件的精确版本号、来源（registry_url: https://components.espressif.com/）、哈希值及目标平台（esp32s3），确保可重复构建。
3. **组件下载与缓存**：LVGL 源码实际以已解析版本（8.3.11）下载到 `managed_components/lvgl__lvgl/` 目录中，由 idf_component_manager 自动拉取与管理。
4. **构建集成**：根级 `CMakeLists.txt` 通过 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` 接入 ESP-IDF 项目模板，自动识别并链接 `managed_components` 下的组件。
5. **无私有仓库配置**：未发现 GOPRIVATE、自定义 registry 或 vendor 策略，所有依赖均从 Espressif 官方公共组件仓库获取。

开发者约定：新增依赖应在 `main/idf_component.yml` 中声明，提交后由 CI 自动生成 `dependencies.lock`；禁止手动修改 `managed_components` 下源码，应通过更新 `idf_component.yml` 的版本约束来升级。