# Reptile Sim Ultimate - Root CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF CMake project setup
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# GCC 15.x workarounds
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=attributes" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=cpp" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)

# Enable C++17 for reptile_core component
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=c++17" APPEND)

# Project name
project(reptile_sim_ultimate)
