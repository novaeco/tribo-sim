idf_component_register(
    SRCS "test_calib.c" "../drivers/calib.c"
    INCLUDE_DIRS "." "../drivers" "../include"
    PRIV_REQUIRES unity nvs_flash
)
