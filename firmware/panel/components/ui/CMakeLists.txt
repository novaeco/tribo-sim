idf_component_register(
    SRCS "ui_main.c" "localization.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config drivers lvgl network
)
