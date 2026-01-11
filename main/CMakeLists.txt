cmake_minimum_required(VERSION 3.5)

# Register the main application component.  We depend on a number
# of internal components (display, input, game, anim, storage) which
# are specified via the REQUIRES field.  INCLUDE_DIRS points to
# directories that should be added to the include search path when
# compiling this component.

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    # depend on sim_display instead of display
    REQUIRES sim_display input game anim storage
)