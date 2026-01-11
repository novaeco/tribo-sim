# Unit test configuration for ESP-IDF Unity framework

# This file is used when running tests via pytest or idf.py
# To run tests: idf.py -T test build flash monitor

idf_component_register(
    SRCS "test_game.c"
    INCLUDE_DIRS "."
    REQUIRES unity types
)
