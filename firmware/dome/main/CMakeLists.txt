idf_component_register(
    SRCS "app_main.c"
         "drivers/i2c_slave_if.c"
         "drivers/ledc_cc.c"
         "drivers/ws2812_rmt.c"
         "drivers/fan_pwm.c"
         "drivers/ntc_adc.c"
         "drivers/uvi_sensor.c"
    INCLUDE_DIRS "include" "drivers"
    REQUIRES driver esp_driver_i2c esp_driver_ledc esp_driver_rmt nvs_flash esp_timer esp_adc
)
