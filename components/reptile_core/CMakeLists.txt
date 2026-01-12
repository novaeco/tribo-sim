idf_component_register(
    SRCS
        "src/reptile_engine.cpp"
        "src/sim_physics.cpp"
        "src/sim_biology.cpp"
        "src/sim_nutrition.cpp"
        "src/sim_genetics.cpp"
        "src/sim_reproduction.cpp"
        "src/sim_behavior.cpp"
        "src/sim_sanitary.cpp"
        "src/sim_seasonal.cpp"
        "src/sim_economy.cpp"
        "src/sim_admin.cpp"
        "src/sim_weather.cpp"
        "src/sim_social.cpp"
        "src/sim_technical.cpp"
        "src/sim_security.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        # Pure C++ - no ESP-IDF driver dependencies
)

# Enable C++17
target_compile_options(${COMPONENT_LIB} PRIVATE "-std=c++17")
