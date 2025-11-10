#include "network_manager.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "controller_cert_store.h"

esp_err_t network_http_event_handler_cb(esp_http_client_event_t *evt)
{
    network_http_response_buffer_t *resp = (network_http_response_buffer_t *)evt->user_data;
    if (!resp) {
        return ESP_OK;
    }
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!evt->data || evt->data_len <= 0) {
            break;
        }
        if (!resp->buffer) {
            resp->capacity = evt->data_len + 1;
            resp->buffer = malloc(resp->capacity);
            if (!resp->buffer) {
                return ESP_ERR_NO_MEM;
            }
            resp->length = 0;
        }
        if (resp->length + evt->data_len + 1 > resp->capacity) {
            int new_capacity = resp->length + evt->data_len + 1;
            char *new_buf = realloc(resp->buffer, new_capacity);
            if (!new_buf) {
                free(resp->buffer);
                resp->buffer = NULL;
                return ESP_ERR_NO_MEM;
            }
            resp->buffer = new_buf;
            resp->capacity = new_capacity;
        }
        memcpy(resp->buffer + resp->length, evt->data, evt->data_len);
        resp->length += evt->data_len;
        resp->buffer[resp->length] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

void network_manager_prepare_http_client_config(const app_config_t *cfg,
                                                const char *path,
                                                esp_http_client_method_t method,
                                                network_http_response_buffer_t *resp,
                                                esp_http_client_config_t *out)
{
    if (!cfg || !path || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->host = cfg->controller_host;
    out->path = path;
    out->port = cfg->controller_port;
    out->disable_auto_redirect = false;
    out->user_data = resp;
    out->event_handler = network_http_event_handler_cb;
    out->timeout_ms = 5000;
    out->method = method;
    out->transport_type = cfg->use_tls ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
    if (cfg->use_tls) {
        out->cert_pem = controller_cert_store_get();
        out->common_name = cfg->controller_host;
        out->skip_cert_common_name_check = false;
    }
}

