idf_component_register(
    SRCS "audio.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_ledc esp_driver_gpio
)
