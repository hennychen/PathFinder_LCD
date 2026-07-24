# ============================================================================
#  CMakeLists.txt — Face Tracker Pipeline (ESP32-S3 + ESP-DL)
#  ESP-IDF v5.0+ 构建配置
# ============================================================================

# 项目级 CMakeLists.txt (放在项目根目录)
# 使用 idf.py build 编译，idf.py flash 烧录

cmake_minimum_required(VERSION 3.16)

# 包含 ESP-IDF 构建系统
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(face_tracker_pipeline)

# ── ESP-DL v3.0-rc 兼容性修复 ──
# ESP-DL 的部分 .cpp 源文件包含 #pragma once（本应仅在头文件中使用），
# ESP-IDF v6.0 默认 -Werror 导致编译失败。
# 全局抑制该特定警告。
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=pragma-once-outside-header" APPEND)
