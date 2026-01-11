idf_component_register(
    SRCS "sim_display.c"
    INCLUDE_DIRS "."
    # Dependencies:
    # - lvgl: Graphics library
    # - esp_lcd: LCD panel drivers
    # - driver: GPIO for backlight control
    # - esp_driver_ledc: LEDC driver for PWM backlight (ESP-IDF 6.x)
    # - types: For ReptileState type
    REQUIRES lvgl esp_lcd driver esp_driver_ledc types
    # Private dependency on game (for g_state access in .c file)
    PRIV_REQUIRES game
)
