idf_component_register(
    SRCS "sim_display.c"
    INCLUDE_DIRS "."
    # Dependencies:
    # - lvgl: Graphics library
    # - esp_lcd: LCD panel drivers
    # - driver: GPIO and LEDC for backlight control
    # - types: For ReptileState type
    REQUIRES lvgl esp_lcd driver types
    # Private dependency on game (for g_state access in .c file)
    PRIV_REQUIRES game
)
