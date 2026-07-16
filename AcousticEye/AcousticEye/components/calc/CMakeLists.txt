idf_component_register(
    SRCS "calc_direction.c"
    INCLUDE_DIRS "."
    REQUIRES esp_dsp driver es7210 esp_timer esp_driver_gpio esp_driver_i2c esp_driver_i2s
)
