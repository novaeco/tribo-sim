idf_component_register(
    SRCS
        "test_http_config.c"
        "test_app_config.c"
        "test_network_manager_faults.c"
        "../components/network/network_manager.c"
        "../components/network/http_utils.c"
        "../components/network/controller_cert_store.c"
    INCLUDE_DIRS "." "../components/network/include" "../components/app_config/include"
    REQUIRES unity esp_http_client app_config
)
