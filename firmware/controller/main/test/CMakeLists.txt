idf_component_register(
    SRCS "test_calib.c" "test_species.c" "test_climate_fail_safe.c" "test_ota_stream.c" "test_dome_bus_concurrency.c" "test_light_payload.c" "../drivers/calib.c" "../drivers/climate.c" "../drivers/dome_bus.c" "../species_profiles.c" "../net/ota_stream.c" "../net/light_payload.c"
    INCLUDE_DIRS "." "../drivers" "../include" "../net"
    PRIV_REQUIRES unity nvs_flash driver
)
