idf_component_register(
    SRCS "esp_lcd_touch_gt911.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_lcd espressif__esp_lcd_touch
)