idf_component_register(
    SRCS "game.c"
    INCLUDE_DIRS "."
    # Public dependencies
    REQUIRES types
    # Private dependencies (only needed for game.c implementation)
    PRIV_REQUIRES storage sim_display
)
