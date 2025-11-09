idf_component_register(
    SRCS "display_driver.c" "touch_gt911.c" "lvgl_port.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_timer lvgl
)

set_source_files_properties(lvgl_port.c PROPERTIES COMPILE_DEFINITIONS LV_CONF_INCLUDE_SIMPLE)
