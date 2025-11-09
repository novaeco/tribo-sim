#include "unity.h"

#include <string.h>

#include "network_manager.h"

TEST_CASE("HTTP config enables TLS root CA when requested", "[network]")
{
    app_config_t cfg = {0};
    strlcpy(cfg.controller_host, "example.com", sizeof(cfg.controller_host));
    cfg.controller_port = 443;
    cfg.use_tls = true;

    network_http_response_buffer_t resp = {0};
    esp_http_client_config_t http_cfg;
    network_manager_prepare_http_client_config(&cfg, "/api/status", HTTP_METHOD_GET, &resp, &http_cfg);

    TEST_ASSERT_EQUAL_UINT16(443, http_cfg.port);
    TEST_ASSERT_EQUAL(HTTP_TRANSPORT_OVER_SSL, http_cfg.transport_type);
    TEST_ASSERT_NOT_NULL(http_cfg.cert_pem);
}

TEST_CASE("HTTP config disables TLS when not requested", "[network]")
{
    app_config_t cfg = {0};
    strlcpy(cfg.controller_host, "10.0.0.1", sizeof(cfg.controller_host));
    cfg.controller_port = 80;
    cfg.use_tls = false;

    network_http_response_buffer_t resp = {0};
    esp_http_client_config_t http_cfg;
    network_manager_prepare_http_client_config(&cfg, "/api/status", HTTP_METHOD_GET, &resp, &http_cfg);

    TEST_ASSERT_EQUAL(HTTP_TRANSPORT_OVER_TCP, http_cfg.transport_type);
    TEST_ASSERT_NULL(http_cfg.cert_pem);
}

