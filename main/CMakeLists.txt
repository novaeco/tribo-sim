cmake_minimum_required(VERSION 3.5)

# Register the main application component with all dependencies

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        sim_display
        input
        game
        anim
        storage
        audio
        ota
        assets
)
