idf_component_register(
    SRCS "network_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config esp_event esp_http_client esp_netif esp_wifi json nvs_flash
)
