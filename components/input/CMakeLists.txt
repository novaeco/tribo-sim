idf_component_register(
    SRCS "input.c"
    INCLUDE_DIRS "."
    # Dependencies:
    # - driver: I2C for GT911 communication
    # - lvgl: For input device registration
    # - esp_lcd_touch_gt911: Touch controller driver
    REQUIRES driver lvgl esp_lcd_touch_gt911
)
