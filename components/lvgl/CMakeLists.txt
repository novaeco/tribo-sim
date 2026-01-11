# This is a wrapper component for LVGL
# The actual LVGL library is fetched via the component manager
# This directory provides the lv_conf.h configuration file

idf_component_register()

# Add the lv_conf.h include path
target_include_directories(${COMPONENT_LIB} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
