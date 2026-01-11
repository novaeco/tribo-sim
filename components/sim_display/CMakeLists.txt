idf_component_register(
    SRCS "sim_display.c"
    INCLUDE_DIRS "."
    # Dependencies:
    # - lvgl_port: Provides lv_conf.h configuration
    # - esp_lcd: LCD panel drivers
    # - esp_driver_gpio: GPIO for backlight control
    # - esp_driver_ledc: LEDC driver for PWM backlight (ESP-IDF 6.x)
    # - types: For ReptileState type
    REQUIRES lvgl_port esp_lcd esp_driver_gpio esp_driver_ledc types
    # Private dependency on game (for g_state access in .c file)
    PRIV_REQUIRES game
)
