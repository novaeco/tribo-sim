idf_component_register(
    SRCS "test_http_config.c" "../components/network/http_utils.c"
    INCLUDE_DIRS "." "../components/network/include" "../components/app_config/include"
    REQUIRES unity esp_http_client app_config
)
