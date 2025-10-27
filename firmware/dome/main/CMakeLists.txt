idf_component_register(
    SRCS "app_main.c"
         "drivers/i2c_slave_if.c"
         "drivers/ledc_cc.c"
         "drivers/ws2812_rmt.c"
         "drivers/fan_pwm.c"
         "drivers/ntc_adc.c"
    INCLUDE_DIRS "include" "drivers"
    REQUIRES driver esp_timer nvs_flash esp_adc
)
