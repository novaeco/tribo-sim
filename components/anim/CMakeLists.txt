idf_component_register(
    SRCS "anim.c"
    INCLUDE_DIRS "."
    # require sim_display for access to the pet_obj and related symbols
    # require lvgl_port for lv_conf.h
    REQUIRES sim_display lvgl_port
)