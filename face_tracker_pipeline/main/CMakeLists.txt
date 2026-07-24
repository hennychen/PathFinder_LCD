# ============================================================================
#  main/CMakeLists.txt — Face Tracker Pipeline 组件注册
#  ESP-IDF v5.0+ 组件级构建配置
# ============================================================================

# 注册源文件 (.c → C 编译, .cc → C++ 编译，ESP-DL 需要 C++ 编译器)
idf_component_register(
    SRCS
        "main_pipeline.c"
        "camera_pipeline.c"
        "servo_pid.c"
        "face_detect_pipeline.cc"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        esp_psram
        esp_timer
        esp_driver_gpio
        esp_driver_ledc
        espressif__esp32-camera
        espressif__esp-dl
        espressif__human_face_detect
)
