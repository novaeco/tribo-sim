idf_component_register(
    SRCS "input.c"
    INCLUDE_DIRS "."
    # Dependencies:
    # - esp_driver_i2c: I2C for GT911 communication
    # - lvgl_port: For LVGL configuration (lv_conf.h)
    # - esp_lcd_touch_gt911: Touch controller driver
    REQUIRES esp_driver_i2c lvgl_port esp_lcd_touch_gt911
)
