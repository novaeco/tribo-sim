idf_component_register(
    SRCS "sim_display.c"
    INCLUDE_DIRS "."
    # Dependencies:
    # - lvgl_port: Provides lv_conf.h configuration
    # - esp_lcd: LCD panel drivers (includes MIPI-DSI bus and DPI/DBI panel drivers for JD9165)
    # - esp_driver_gpio: GPIO for backlight control
    # - esp_driver_ledc: LEDC driver for PWM backlight (ESP-IDF 6.x)
    # - esp_hw_support: For LDO regulator API (MIPI DPHY power)
    # - types: For ReptileState type
    REQUIRES lvgl_port esp_lcd esp_driver_gpio esp_driver_ledc esp_hw_support types
    # Private dependency on game (for g_state access in .c file)
    PRIV_REQUIRES game
)
