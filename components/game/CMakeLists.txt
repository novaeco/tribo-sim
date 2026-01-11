idf_component_register(
    SRCS "game.c"
    INCLUDE_DIRS "."
    # require the storage component for persistence and sim_display for UI updates
    REQUIRES storage sim_display
)