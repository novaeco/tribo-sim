idf_component_register(
    SRCS "anim.c"
    INCLUDE_DIRS "."
    # require sim_display for access to the pet_obj and related symbols
    REQUIRES sim_display
)