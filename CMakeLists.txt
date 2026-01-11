cmake_minimum_required(VERSION 3.5)

# This top‑level CMakeLists.txt is required by ESP‑IDF.  It
# specifies the project name and includes the project.cmake file from
# the ESP‑IDF build system.  All components and the main application
# are discovered automatically in subdirectories.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reptile_sim)