cmake_minimum_required(VERSION 3.16)

# The project name must match the folder name when using idf.py.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Workarounds for ESP-IDF v6.1-dev + GCC 15.x:
# 1. LVGL can trigger -Werror=attributes due to IRAM section attribute conflicts
#    when CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y.
# 2. LVGL demos (benchmark) emit #warning which fails with -Werror.
# 3. Bluedroid has stringop-truncation warnings in bt_types.h
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=attributes" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=cpp" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)

project(tribo_sim)
