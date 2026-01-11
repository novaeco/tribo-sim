idf_component_register(
    SRCS "sim_display.c"
    INCLUDE_DIRS "."
    # The display subsystem depends on LVGL for graphics and esp_lcd for
    # driving the JD9165 panel.  No other external component dependencies
    # are required.
    REQUIRES lvgl esp_lcd
)