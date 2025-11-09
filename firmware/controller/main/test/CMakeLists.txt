idf_component_register(
    SRCS "test_calib.c" "test_species.c" "test_climate_fail_safe.c" "test_ota_stream.c" "../drivers/calib.c" "../drivers/climate.c" "../species_profiles.c" "../net/ota_stream.c"
    INCLUDE_DIRS "." "../drivers" "../include"
    PRIV_REQUIRES unity nvs_flash
)
