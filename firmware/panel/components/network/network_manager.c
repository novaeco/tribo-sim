#include "network_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "controller_cert_store.h"

#define TAG "net"

#define WIFI_CONNECTED_BIT BIT0

#define NETWORK_QUEUE_LENGTH       12
#define NETWORK_CMD_TIMEOUT_MS     2000
#define NETWORK_STATUS_INTERVAL_MS 2000
#define NETWORK_RECONNECT_MIN_MS   500
#define NETWORK_RECONNECT_MAX_MS   15000
#define NETWORK_UPLOAD_CHUNK       2048
#define NETWORK_MAX_PATH           256
#define NETWORK_CERT_BACKOFF_MIN_MS 2000
#define NETWORK_CERT_BACKOFF_MAX_MS 60000
#define NETWORK_CERT_PROVISION_PATH "/api/security/root_ca"
#define NETWORK_CERT_PROVISION_TIMEOUT_MS 8000

typedef enum {
    NETWORK_STATE_STOPPED = 0,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_ERROR,
} network_state_t;

typedef enum {
    NETWORK_CMD_LIGHT_POST = 0,
    NETWORK_CMD_CALIB_GET,
    NETWORK_CMD_CALIB_POST,
    NETWORK_CMD_MUTE_SET,
    NETWORK_CMD_SPECIES_FETCH,
    NETWORK_CMD_SPECIES_APPLY,
    NETWORK_CMD_OTA_CONTROLLER,
    NETWORK_CMD_OTA_DOME,
    NETWORK_CMD_CERT_PROVISION,
} network_command_type_t;

typedef struct {
    network_command_type_t type;
    union {
        terrarium_light_command_t light;
        terrarium_uvb_calibration_command_t calib;
        bool mute;
        struct {
            char key[NETWORK_MAX_SPECIES_KEY];
        } species;
        struct {
            char path[NETWORK_MAX_PATH];
        } ota;
        struct {
            QueueHandle_t response;
        } provision;
    } payload;
} network_command_t;

typedef struct {
    app_config_t config;
    bool wifi_initialized;
    bool started;
    network_state_t state;
    EventGroupHandle_t wifi_events;
    QueueHandle_t command_queue;
    TaskHandle_t task_handle;
    esp_event_handler_instance_t wifi_any_handle;
    esp_event_handler_instance_t ip_got_handle;
    esp_timer_handle_t reconnect_timer;
    uint32_t reconnect_backoff_ms;
    terrarium_status_t status;
    terrarium_species_catalog_t species;
    bool species_loaded;
    network_status_cb_t status_cb;
    void *status_ctx;
    network_error_cb_t error_cb;
    void *error_ctx;
    network_species_cb_t species_cb;
    void *species_ctx;
    bool cert_auto_pending;
    uint32_t cert_backoff_ms;
    uint64_t cert_next_attempt_us;
} network_context_t;

static network_context_t s_ctx = {0};

static void network_task(void *arg);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void reconnect_timer_cb(void *arg);
static void network_set_state(network_state_t state);
static void network_report_error(esp_err_t err, const char *msg);
static void network_invoke_status_cb(void);
static void network_invoke_species_cb(void);
static esp_err_t network_fetch_status(void);
static esp_err_t network_fetch_calibration_internal(void);
static esp_err_t network_post_light_internal(const terrarium_light_command_t *cmd);
static esp_err_t network_post_calibration_internal(const terrarium_uvb_calibration_command_t *cmd);
static esp_err_t network_set_alarm_mute_internal(bool mute);
static esp_err_t network_fetch_species_internal(void);
static esp_err_t network_apply_species_internal(const char *key);
static esp_err_t network_upload_ota_internal(const char *endpoint, const char *path);
static esp_err_t network_import_root_ca_from_buffer_internal(const uint8_t *data, size_t len);
static esp_err_t network_provision_root_ca_internal(void);
static esp_err_t http_perform(const char *path,
                              esp_http_client_method_t method,
                              const char *payload,
                              size_t payload_len,
                              char **out_body,
                              int *out_len);
static void parse_status_json(const char *json, terrarium_status_t *out_status);
static esp_err_t parse_species_json(const char *json, terrarium_species_catalog_t *out_catalog);
static void network_update_certificate_plan(void);

static void network_update_certificate_plan(void)
{
    if (!s_ctx.config.use_tls) {
        s_ctx.cert_auto_pending = false;
        s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
        s_ctx.cert_next_attempt_us = 0;
        return;
    }
    if (controller_cert_store_has_custom()) {
        s_ctx.cert_auto_pending = false;
        s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
        s_ctx.cert_next_attempt_us = 0;
        return;
    }
    if (s_ctx.config.auto_provision_root_ca) {
        if (s_ctx.cert_backoff_ms == 0) {
            s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
        }
        s_ctx.cert_auto_pending = true;
    } else {
        s_ctx.cert_auto_pending = false;
    }
}

esp_err_t network_manager_init(const app_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    controller_cert_store_init();
    app_config_t new_cfg = *config;
    if (s_ctx.started) {
        bool wifi_credentials_changed = (strncmp(s_ctx.config.ssid, new_cfg.ssid, sizeof(new_cfg.ssid)) != 0) ||
                                        (strncmp(s_ctx.config.password, new_cfg.password, sizeof(new_cfg.password)) != 0);
        bool host_changed = (strncmp(s_ctx.config.controller_host, new_cfg.controller_host, sizeof(new_cfg.controller_host)) !=
                             0) ||
                            (s_ctx.config.controller_port != new_cfg.controller_port) ||
                            (s_ctx.config.use_tls != new_cfg.use_tls);
        s_ctx.config = new_cfg;
        if (host_changed) {
            s_ctx.species_loaded = false;
            memset(&s_ctx.species, 0, sizeof(s_ctx.species));
            s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
            s_ctx.cert_next_attempt_us = 0;
        }
        if (wifi_credentials_changed && s_ctx.wifi_initialized) {
            wifi_config_t wifi_cfg = {0};
            strlcpy((char *)wifi_cfg.sta.ssid, new_cfg.ssid, sizeof(wifi_cfg.sta.ssid));
            strlcpy((char *)wifi_cfg.sta.password, new_cfg.password, sizeof(wifi_cfg.sta.password));
            wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            wifi_cfg.sta.sae_pk_mode = WPA3_SAE_PK_MODE_AUTOMATIC;
            ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "disconnect before reconfig");
            ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "update wifi cfg");
            ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "reconnect wifi");
            network_set_state(NETWORK_STATE_CONNECTING);
        }
        network_update_certificate_plan();
        return ESP_OK;
    }
    s_ctx.config = new_cfg;
    s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
    s_ctx.cert_next_attempt_us = 0;
    network_update_certificate_plan();
    return network_manager_start(&s_ctx.config);
}

esp_err_t network_manager_start(const app_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_ctx.config = *config;
    memset(&s_ctx.status, 0, sizeof(s_ctx.status));
    memset(&s_ctx.species, 0, sizeof(s_ctx.species));
    s_ctx.species_loaded = false;

    if (!s_ctx.wifi_initialized) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_cfg), TAG, "wifi init");
        s_ctx.wifi_initialized = true;
    }

    if (!s_ctx.wifi_events) {
        s_ctx.wifi_events = xEventGroupCreate();
        if (!s_ctx.wifi_events) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_ctx.command_queue) {
        s_ctx.command_queue = xQueueCreate(NETWORK_QUEUE_LENGTH, sizeof(network_command_t));
        if (!s_ctx.command_queue) {
            vEventGroupDelete(s_ctx.wifi_events);
            s_ctx.wifi_events = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_ctx.wifi_any_handle), TAG,
        "wifi handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_ctx.ip_got_handle), TAG,
        "ip handler");

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, config->ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, config->password, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_cfg.sta.sae_pk_mode = WPA3_SAE_PK_MODE_AUTOMATIC;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");

    if (!s_ctx.reconnect_timer) {
        const esp_timer_create_args_t args = {
            .callback = &reconnect_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "net_reconnect",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_ctx.reconnect_timer), TAG, "reconnect timer");
    }

    if (!s_ctx.task_handle) {
        BaseType_t ok = xTaskCreatePinnedToCore(network_task, "net_task", 8192, NULL, 5, &s_ctx.task_handle, 0);
        if (ok != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_ctx.started = true;
    s_ctx.reconnect_backoff_ms = NETWORK_RECONNECT_MIN_MS;
    network_set_state(NETWORK_STATE_CONNECTING);
    return ESP_OK;
}

esp_err_t network_manager_stop(void)
{
    if (!s_ctx.started) {
        return ESP_OK;
    }
    s_ctx.started = false;

    if (s_ctx.reconnect_timer) {
        esp_timer_stop(s_ctx.reconnect_timer);
        esp_timer_delete(s_ctx.reconnect_timer);
        s_ctx.reconnect_timer = NULL;
    }

    if (s_ctx.task_handle) {
        vTaskDelete(s_ctx.task_handle);
        s_ctx.task_handle = NULL;
    }

    if (s_ctx.command_queue) {
        vQueueDelete(s_ctx.command_queue);
        s_ctx.command_queue = NULL;
    }

    if (s_ctx.wifi_events) {
        vEventGroupDelete(s_ctx.wifi_events);
        s_ctx.wifi_events = NULL;
    }

    if (s_ctx.wifi_any_handle) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_ctx.wifi_any_handle);
        s_ctx.wifi_any_handle = NULL;
    }
    if (s_ctx.ip_got_handle) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ctx.ip_got_handle);
        s_ctx.ip_got_handle = NULL;
    }

    if (s_ctx.wifi_initialized) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }

    memset(&s_ctx.status, 0, sizeof(s_ctx.status));
    memset(&s_ctx.species, 0, sizeof(s_ctx.species));
    s_ctx.species_loaded = false;
    s_ctx.reconnect_backoff_ms = NETWORK_RECONNECT_MIN_MS;
    network_set_state(NETWORK_STATE_STOPPED);
    return ESP_OK;
}

esp_err_t network_manager_register_status_callback(network_status_cb_t cb, void *ctx)
{
    s_ctx.status_cb = cb;
    s_ctx.status_ctx = ctx;
    if (cb && s_ctx.status.valid) {
        cb(&s_ctx.status, ctx);
    }
    return ESP_OK;
}

void network_manager_register_error_callback(network_error_cb_t cb, void *ctx)
{
    s_ctx.error_cb = cb;
    s_ctx.error_ctx = ctx;
}

void network_manager_register_species_callback(network_species_cb_t cb, void *ctx)
{
    s_ctx.species_cb = cb;
    s_ctx.species_ctx = ctx;
    if (cb && s_ctx.species.count > 0) {
        cb(&s_ctx.species, ctx);
    }
}

esp_err_t network_manager_post_light(const terrarium_light_command_t *cmd)
{
    if (!cmd || !s_ctx.command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {.type = NETWORK_CMD_LIGHT_POST, .payload.light = *cmd};
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_fetch_calibration(void)
{
    if (!s_ctx.command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {.type = NETWORK_CMD_CALIB_GET};
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_post_calibration(const terrarium_uvb_calibration_command_t *cmd)
{
    if (!cmd || !s_ctx.command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {.type = NETWORK_CMD_CALIB_POST, .payload.calib = *cmd};
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_set_alarm_mute(bool mute)
{
    if (!s_ctx.command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {.type = NETWORK_CMD_MUTE_SET, .payload.mute = mute};
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_request_species_catalog(void)
{
    if (!s_ctx.command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {.type = NETWORK_CMD_SPECIES_FETCH};
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_apply_species(const char *key)
{
    if (!key || key[0] == '\0' || !s_ctx.command_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    network_command_t command = {.type = NETWORK_CMD_SPECIES_APPLY};
    strlcpy(command.payload.species.key, key, sizeof(command.payload.species.key));
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t enqueue_ota_command(network_command_type_t type, const char *path)
{
    if (!path || !s_ctx.command_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    network_command_t command = {.type = type};
    strlcpy(command.payload.ota.path, path, sizeof(command.payload.ota.path));
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_upload_controller_ota(const char *path)
{
    return enqueue_ota_command(NETWORK_CMD_OTA_CONTROLLER, path);
}

esp_err_t network_manager_upload_dome_ota(const char *path)
{
    return enqueue_ota_command(NETWORK_CMD_OTA_DOME, path);
}

esp_err_t network_manager_import_root_ca_from_buffer(const uint8_t *data, size_t len)
{
    return network_import_root_ca_from_buffer_internal(data, len);
}

esp_err_t network_manager_import_root_ca_from_file(const char *path)
{
    if (!path || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = controller_cert_store_import_from_file(path);
    if (err == ESP_OK) {
        s_ctx.cert_auto_pending = false;
        s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
        s_ctx.cert_next_attempt_us = 0;
        network_update_certificate_plan();
    }
    return err;
}

esp_err_t network_manager_auto_provision_root_ca(void)
{
    if (!s_ctx.command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    QueueHandle_t response = xQueueCreate(1, sizeof(esp_err_t));
    if (!response) {
        return ESP_ERR_NO_MEM;
    }
    network_command_t command = {.type = NETWORK_CMD_CERT_PROVISION};
    command.payload.provision.response = response;
    if (xQueueSend(s_ctx.command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        vQueueDelete(response);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_ERR_TIMEOUT;
    if (xQueueReceive(response, &result, pdMS_TO_TICKS(NETWORK_CERT_PROVISION_TIMEOUT_MS)) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    }
    vQueueDelete(response);
    return result;
}

void network_manager_get_root_ca_status(network_root_ca_status_t *status)
{
    if (!status) {
        return;
    }
    controller_cert_store_init();
    status->custom = controller_cert_store_has_custom();
    const char *cert = controller_cert_store_get();
    status->available = (cert != NULL);
    status->length = cert ? controller_cert_store_length() : 0;
}

const terrarium_status_t *network_manager_get_cached_status(void)
{
    return s_ctx.status.valid ? &s_ctx.status : NULL;
}

const terrarium_species_catalog_t *network_manager_get_cached_species(void)
{
    return (s_ctx.species.count > 0) ? &s_ctx.species : NULL;
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

static void network_task(void *arg)
{
    (void)arg;
    TickType_t last_poll = xTaskGetTickCount();
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_ctx.wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(200));
        if (!(bits & WIFI_CONNECTED_BIT)) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (s_ctx.config.use_tls && s_ctx.cert_auto_pending && !controller_cert_store_has_custom()) {
            uint64_t now_us = esp_timer_get_time();
            if (s_ctx.cert_next_attempt_us == 0 || now_us >= s_ctx.cert_next_attempt_us) {
                esp_err_t err = network_provision_root_ca_internal();
                if (err == ESP_OK) {
                    s_ctx.cert_auto_pending = false;
                    s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
                    s_ctx.cert_next_attempt_us = 0;
                    continue;
                }
                s_ctx.cert_next_attempt_us = now_us + (uint64_t)s_ctx.cert_backoff_ms * 1000ULL;
                s_ctx.cert_backoff_ms = MIN(s_ctx.cert_backoff_ms * 2, NETWORK_CERT_BACKOFF_MAX_MS);
                network_report_error(err, "cert provision failed");
                network_set_state(NETWORK_STATE_CONNECTING);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        const TickType_t now = xTaskGetTickCount();
        if (pdTICKS_TO_MS(now - last_poll) >= NETWORK_STATUS_INTERVAL_MS) {
            if (network_fetch_status() == ESP_OK && !s_ctx.species_loaded) {
                if (network_fetch_species_internal() == ESP_OK) {
                    s_ctx.species_loaded = true;
                }
            }
            last_poll = now;
        }

        network_command_t command;
        if (xQueueReceive(s_ctx.command_queue, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
            esp_err_t err = ESP_OK;
            switch (command.type) {
            case NETWORK_CMD_LIGHT_POST:
                err = network_post_light_internal(&command.payload.light);
                break;
            case NETWORK_CMD_CALIB_GET:
                err = network_fetch_calibration_internal();
                break;
            case NETWORK_CMD_CALIB_POST:
                err = network_post_calibration_internal(&command.payload.calib);
                break;
            case NETWORK_CMD_MUTE_SET:
                err = network_set_alarm_mute_internal(command.payload.mute);
                break;
            case NETWORK_CMD_SPECIES_FETCH:
                err = network_fetch_species_internal();
                break;
            case NETWORK_CMD_SPECIES_APPLY:
                err = network_apply_species_internal(command.payload.species.key);
                if (err == ESP_OK) {
                    network_fetch_status();
                }
                break;
            case NETWORK_CMD_OTA_CONTROLLER:
                err = network_upload_ota_internal("/api/ota/controller", command.payload.ota.path);
                break;
            case NETWORK_CMD_OTA_DOME:
                err = network_upload_ota_internal("/api/ota/dome", command.payload.ota.path);
                break;
            case NETWORK_CMD_CERT_PROVISION: {
                esp_err_t result = network_provision_root_ca_internal();
                if (command.payload.provision.response) {
                    xQueueSend(command.payload.provision.response, &result, 0);
                }
                if (result == ESP_OK) {
                    s_ctx.cert_auto_pending = false;
                    s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
                    s_ctx.cert_next_attempt_us = 0;
                }
                err = result;
                break;
            }
            default:
                break;
            }
            if (err != ESP_OK) {
                network_report_error(err, "command failed");
            }
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        network_set_state(NETWORK_STATE_CONNECTING);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_ctx.wifi_events, WIFI_CONNECTED_BIT);
        network_set_state(NETWORK_STATE_DISCONNECTED);
        if (s_ctx.reconnect_timer) {
            uint64_t timeout = s_ctx.reconnect_backoff_ms * 1000ULL;
            esp_timer_stop(s_ctx.reconnect_timer);
            esp_timer_start_once(s_ctx.reconnect_timer, timeout);
            s_ctx.reconnect_backoff_ms = MIN(s_ctx.reconnect_backoff_ms * 2, NETWORK_RECONNECT_MAX_MS);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_ctx.wifi_events, WIFI_CONNECTED_BIT);
        s_ctx.reconnect_backoff_ms = NETWORK_RECONNECT_MIN_MS;
        if (s_ctx.reconnect_timer) {
            esp_timer_stop(s_ctx.reconnect_timer);
        }
        network_set_state(NETWORK_STATE_CONNECTED);
    }
}

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "Reconnecting Wi-Fi");
    network_set_state(NETWORK_STATE_CONNECTING);
    esp_wifi_connect();
}

static void network_set_state(network_state_t state)
{
    if (s_ctx.state == state) {
        return;
    }
    s_ctx.state = state;
    switch (state) {
    case NETWORK_STATE_STOPPED:
        ESP_LOGI(TAG, "state=STOPPED");
        break;
    case NETWORK_STATE_DISCONNECTED:
        ESP_LOGW(TAG, "state=DISCONNECTED");
        break;
    case NETWORK_STATE_CONNECTING:
        ESP_LOGI(TAG, "state=CONNECTING");
        break;
    case NETWORK_STATE_CONNECTED:
        ESP_LOGI(TAG, "state=CONNECTED");
        break;
    case NETWORK_STATE_ERROR:
        ESP_LOGE(TAG, "state=ERROR");
        break;
    }
}

static void network_report_error(esp_err_t err, const char *msg)
{
    network_set_state(NETWORK_STATE_ERROR);
    if (s_ctx.error_cb) {
        s_ctx.error_cb(err, msg, s_ctx.error_ctx);
    }
}

static void network_invoke_status_cb(void)
{
    if (s_ctx.status_cb && s_ctx.status.valid) {
        s_ctx.status_cb(&s_ctx.status, s_ctx.status_ctx);
    }
}

static void network_invoke_species_cb(void)
{
    if (s_ctx.species_cb && s_ctx.species.count > 0) {
        s_ctx.species_cb(&s_ctx.species, s_ctx.species_ctx);
    }
}

static esp_err_t network_import_root_ca_from_buffer_internal(const uint8_t *data, size_t len)
{
    esp_err_t err = controller_cert_store_import(data, len);
    if (err == ESP_OK) {
        s_ctx.cert_auto_pending = false;
        s_ctx.cert_backoff_ms = NETWORK_CERT_BACKOFF_MIN_MS;
        s_ctx.cert_next_attempt_us = 0;
        network_update_certificate_plan();
    }
    return err;
}

static esp_err_t network_fetch_root_ca(uint16_t port, network_http_response_buffer_t *resp)
{
    esp_http_client_config_t cfg = {0};
    cfg.host = s_ctx.config.controller_host;
    cfg.path = NETWORK_CERT_PROVISION_PATH;
    cfg.port = port;
    cfg.disable_auto_redirect = false;
    cfg.timeout_ms = 5000;
    cfg.method = HTTP_METHOD_GET;
    cfg.event_handler = network_http_event_handler_cb;
    cfg.user_data = resp;
    cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            err = ESP_FAIL;
        }
    }
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t network_provision_root_ca_internal(void)
{
    network_http_response_buffer_t resp = {0};
    esp_err_t err = network_fetch_root_ca(s_ctx.config.controller_port, &resp);
    if (err != ESP_OK && s_ctx.config.controller_port != 80) {
        if (resp.buffer) {
            free(resp.buffer);
            resp.buffer = NULL;
            resp.length = 0;
            resp.capacity = 0;
        }
        err = network_fetch_root_ca(80, &resp);
    }
    if (err != ESP_OK) {
        if (resp.buffer) {
            free(resp.buffer);
        }
        return err;
    }
    if (!resp.buffer || resp.length == 0) {
        if (resp.buffer) {
            free(resp.buffer);
        }
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t import_err = network_import_root_ca_from_buffer_internal((const uint8_t *)resp.buffer, resp.length + 1);
    if (import_err == ESP_OK) {
        ESP_LOGI(TAG, "Root CA provisioned (%d bytes)", resp.length);
    }
    free(resp.buffer);
    return import_err;
}

static esp_err_t http_perform(const char *path,
                              esp_http_client_method_t method,
                              const char *payload,
                              size_t payload_len,
                              char **out_body,
                              int *out_len)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    network_http_response_buffer_t resp = {0};
    esp_http_client_config_t cfg;
    network_manager_prepare_http_client_config(&s_ctx.config, path, method, &resp, &cfg);

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    if (method == HTTP_METHOD_POST || method == HTTP_METHOD_PUT) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if (payload && payload_len > 0) {
        esp_http_client_set_post_field(client, payload, payload_len);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status >= 400) {
            err = ESP_FAIL;
        }
    }

    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        if (resp.buffer) {
            free(resp.buffer);
        }
        return err;
    }

    if (out_body) {
        if (resp.buffer) {
            *out_body = resp.buffer;
        } else {
            *out_body = calloc(1, 1);
            if (!*out_body) {
                return ESP_ERR_NO_MEM;
            }
        }
    } else if (resp.buffer) {
        free(resp.buffer);
    }
    if (out_len) {
        *out_len = resp.length;
    }
    return ESP_OK;
}

static esp_err_t network_fetch_status(void)
{
    char *body = NULL;
    int len = 0;
    esp_err_t err = http_perform("/api/status", HTTP_METHOD_GET, NULL, 0, &body, &len);
    if (err != ESP_OK) {
        return err;
    }
    parse_status_json(body, &s_ctx.status);
    free(body);
    if (!s_ctx.status.valid) {
        return ESP_FAIL;
    }
    network_invoke_status_cb();
    return ESP_OK;
}

static esp_err_t network_fetch_calibration_internal(void)
{
    char *body = NULL;
    int len = 0;
    esp_err_t err = http_perform("/api/calibrate/uvb", HTTP_METHOD_GET, NULL, 0, &body, &len);
    if (err != ESP_OK) {
        return err;
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        free(body);
        return ESP_FAIL;
    }
    terrarium_uvb_calibration_t *cal = &s_ctx.status.uvb_calibration;
    memset(cal, 0, sizeof(*cal));
    cJSON *k = cJSON_GetObjectItem(root, "k");
    cJSON *uvi_max = cJSON_GetObjectItem(root, "uvi_max");
    if (cJSON_IsNumber(k) && cJSON_IsNumber(uvi_max)) {
        cal->valid = true;
        cal->k = k->valuedouble;
        cal->uvi_max = uvi_max->valuedouble;
    }
    cJSON_Delete(root);
    free(body);
    network_invoke_status_cb();
    return ESP_OK;
}

static esp_err_t network_post_light_internal(const terrarium_light_command_t *cmd)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON *cct = cJSON_AddObjectToObject(root, "cct");
    cJSON *uva = cJSON_AddObjectToObject(root, "uva");
    cJSON *uvb = cJSON_AddObjectToObject(root, "uvb");
    if (!cct || !uva || !uvb) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(cct, "day", cmd->cct_day);
    cJSON_AddNumberToObject(cct, "warm", cmd->cct_warm);
    cJSON_AddNumberToObject(uva, "set", cmd->uva_set);
    cJSON_AddNumberToObject(uva, "clamp", cmd->uva_clamp);
    cJSON_AddNumberToObject(uvb, "set", cmd->uvb_set);
    cJSON_AddNumberToObject(uvb, "clamp", cmd->uvb_clamp);
    cJSON_AddNumberToObject(uvb, "period_s", cmd->uvb_period_s);
    cJSON_AddNumberToObject(uvb, "duty_pm", cmd->uvb_duty_pm);
    cJSON_AddNumberToObject(root, "sky", cmd->sky);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = http_perform("/api/light/dome0", HTTP_METHOD_POST, payload, strlen(payload), NULL, NULL);
    free(payload);
    return err;
}

static esp_err_t network_post_calibration_internal(const terrarium_uvb_calibration_command_t *cmd)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "k", cmd->k);
    cJSON_AddNumberToObject(root, "uvi_max", cmd->uvi_max);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = http_perform("/api/calibrate/uvb", HTTP_METHOD_POST, payload, strlen(payload), NULL, NULL);
    free(payload);
    return err;
}

static esp_err_t network_set_alarm_mute_internal(bool mute)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(root, "mute", mute);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = http_perform("/api/alarms/mute", HTTP_METHOD_POST, payload, strlen(payload), NULL, NULL);
    free(payload);
    if (err == ESP_OK) {
        s_ctx.status.alarm_muted = mute;
        network_invoke_status_cb();
    }
    return err;
}

static esp_err_t network_fetch_species_internal(void)
{
    char *body = NULL;
    int len = 0;
    esp_err_t err = http_perform("/api/species", HTTP_METHOD_GET, NULL, 0, &body, &len);
    if (err != ESP_OK) {
        return err;
    }
    terrarium_species_catalog_t catalog = {0};
    err = parse_species_json(body, &catalog);
    free(body);
    if (err != ESP_OK) {
        return err;
    }
    s_ctx.species = catalog;
    network_invoke_species_cb();
    return ESP_OK;
}

static esp_err_t network_apply_species_internal(const char *key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "key", key);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = http_perform("/api/species/apply", HTTP_METHOD_POST, payload, strlen(payload), NULL, NULL);
    free(payload);
    if (err == ESP_OK) {
        strlcpy(s_ctx.species.active_key, key, sizeof(s_ctx.species.active_key));
        network_invoke_species_cb();
    }
    return err;
}

static esp_err_t network_upload_ota_internal(const char *endpoint, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s: errno=%d", path, errno);
        return ESP_FAIL;
    }
    esp_http_client_config_t cfg;
    network_manager_prepare_http_client_config(&s_ctx.config, endpoint, HTTP_METHOD_POST, NULL, &cfg);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        fclose(f);
        return err;
    }
    uint8_t *buffer = malloc(NETWORK_UPLOAD_CHUNK);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t total = 0;
    while (1) {
        size_t read = fread(buffer, 1, NETWORK_UPLOAD_CHUNK, f);
        if (read > 0) {
            int written = esp_http_client_write(client, (const char *)buffer, read);
            if (written < 0) {
                err = ESP_FAIL;
                break;
            }
            total += written;
        }
        if (read < NETWORK_UPLOAD_CHUNK) {
            if (feof(f)) {
                break;
            }
            if (ferror(f)) {
                err = ESP_FAIL;
                break;
            }
        }
    }
    free(buffer);
    fclose(f);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status >= 400) {
            err = ESP_FAIL;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Uploaded %s (%zu bytes)", endpoint, total);
    }
    return err;
}

static void parse_status_json(const char *json, terrarium_status_t *out_status)
{
    if (!json || !out_status) {
        return;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        out_status->valid = false;
        return;
    }
    memset(out_status, 0, sizeof(*out_status));
    out_status->timestamp_ms = esp_timer_get_time() / 1000ULL;

    cJSON *summary = cJSON_GetObjectItem(root, "summary");
    if (cJSON_IsString(summary) && summary->valuestring) {
        strlcpy(out_status->summary, summary->valuestring, sizeof(out_status->summary));
    }

    cJSON *env = cJSON_GetObjectItem(root, "env");
    if (cJSON_IsObject(env)) {
        cJSON *t = cJSON_GetObjectItem(env, "temperature");
        cJSON *h = cJSON_GetObjectItem(env, "humidity");
        cJSON *p = cJSON_GetObjectItem(env, "pressure");
        cJSON *u = cJSON_GetObjectItem(env, "uvi");
        cJSON *irr = cJSON_GetObjectItem(env, "irradiance_uW_cm2");
        out_status->env.valid = true;
        if (cJSON_IsNumber(t)) {
            out_status->env.temperature_c = t->valuedouble;
        }
        if (cJSON_IsNumber(h)) {
            out_status->env.humidity_percent = h->valuedouble;
        }
        if (cJSON_IsNumber(p)) {
            out_status->env.pressure_hpa = p->valuedouble;
        }
        if (cJSON_IsNumber(u)) {
            out_status->env.uvi = u->valuedouble;
        }
        if (cJSON_IsNumber(irr)) {
            out_status->env.irradiance_uW_cm2 = irr->valuedouble;
        }
    }

    cJSON *light = cJSON_GetObjectItem(root, "light");
    if (cJSON_IsObject(light)) {
        out_status->light.valid = true;
        cJSON *cct = cJSON_GetObjectItem(light, "cct");
        if (cJSON_IsObject(cct)) {
            cJSON *day = cJSON_GetObjectItem(cct, "day");
            cJSON *warm = cJSON_GetObjectItem(cct, "warm");
            if (cJSON_IsNumber(day)) {
                out_status->light.cct_day = day->valuedouble;
            }
            if (cJSON_IsNumber(warm)) {
                out_status->light.cct_warm = warm->valuedouble;
            }
        }
        cJSON *uva = cJSON_GetObjectItem(light, "uva");
        if (cJSON_IsObject(uva)) {
            cJSON *set = cJSON_GetObjectItem(uva, "set");
            cJSON *clamp = cJSON_GetObjectItem(uva, "clamp");
            if (cJSON_IsNumber(set)) {
                out_status->light.uva_set = set->valuedouble;
            }
            if (cJSON_IsNumber(clamp)) {
                out_status->light.uva_clamp = clamp->valuedouble;
            }
        }
        cJSON *uvb = cJSON_GetObjectItem(light, "uvb");
        if (cJSON_IsObject(uvb)) {
            cJSON *set = cJSON_GetObjectItem(uvb, "set");
            cJSON *clamp = cJSON_GetObjectItem(uvb, "clamp");
            cJSON *period = cJSON_GetObjectItem(uvb, "period_s");
            cJSON *duty = cJSON_GetObjectItem(uvb, "duty_pm");
            if (cJSON_IsNumber(set)) {
                out_status->light.uvb_set = set->valuedouble;
            }
            if (cJSON_IsNumber(clamp)) {
                out_status->light.uvb_clamp = clamp->valuedouble;
            }
            if (cJSON_IsNumber(period)) {
                out_status->light.uvb_period_s = period->valuedouble;
            }
            if (cJSON_IsNumber(duty)) {
                out_status->light.uvb_duty_pm = duty->valuedouble;
            }
        }
        cJSON *sky = cJSON_GetObjectItem(light, "sky");
        if (cJSON_IsNumber(sky)) {
            out_status->light.sky_mode = sky->valuedouble;
        }
        cJSON *fan = cJSON_GetObjectItem(light, "fan_pwm");
        if (cJSON_IsNumber(fan)) {
            out_status->light.fan_pwm_percent = fan->valuedouble;
        }
    }

    cJSON *dome = cJSON_GetObjectItem(root, "dome");
    if (cJSON_IsObject(dome)) {
        out_status->dome.valid = true;
        cJSON *status = cJSON_GetObjectItem(dome, "status");
        cJSON *flags = cJSON_GetObjectItem(dome, "flags");
        cJSON *heatsink = cJSON_GetObjectItem(dome, "heatsink_c");
        cJSON *uvi = cJSON_GetObjectItem(dome, "uvi");
        cJSON *irr = cJSON_GetObjectItem(dome, "irradiance_uW_cm2");
        cJSON *fault = cJSON_GetObjectItem(dome, "uvi_fault");
        if (cJSON_IsNumber(status)) {
            out_status->dome.status = status->valueint;
        }
        if (cJSON_IsNumber(flags)) {
            out_status->dome.flags = flags->valueint;
        }
        if (cJSON_IsNumber(heatsink)) {
            out_status->dome.heatsink_c = heatsink->valuedouble;
        }
        if (cJSON_IsNumber(uvi)) {
            out_status->dome.uvi = uvi->valuedouble;
        }
        if (cJSON_IsNumber(irr)) {
            out_status->dome.irradiance_uW_cm2 = irr->valuedouble;
        }
        if (cJSON_IsBool(fault)) {
            out_status->dome.uvi_fault = cJSON_IsTrue(fault);
        }
    }

    cJSON *climate = cJSON_GetObjectItem(root, "climate");
    if (cJSON_IsObject(climate)) {
        out_status->climate.valid = true;
        cJSON *heater = cJSON_GetObjectItem(climate, "heater_on");
        cJSON *lights_on = cJSON_GetObjectItem(climate, "lights_on");
        cJSON *fail_safe = cJSON_GetObjectItem(climate, "fail_safe_active");
        cJSON *temp_sp = cJSON_GetObjectItem(climate, "temp_setpoint");
        cJSON *hum_sp = cJSON_GetObjectItem(climate, "humidity_setpoint");
        cJSON *uvi_target = cJSON_GetObjectItem(climate, "uvi_target");
        cJSON *uvi_valid = cJSON_GetObjectItem(climate, "uvi_valid");
        cJSON *uvi_measured = cJSON_GetObjectItem(climate, "uvi_measured");
        cJSON *uvi_error = cJSON_GetObjectItem(climate, "uvi_error");
        cJSON *irradiance = cJSON_GetObjectItem(climate, "irradiance_uW_cm2");
        if (cJSON_IsBool(heater)) {
            out_status->climate.heater_on = cJSON_IsTrue(heater);
        }
        if (cJSON_IsBool(lights_on)) {
            out_status->climate.lights_on = cJSON_IsTrue(lights_on);
        }
        if (cJSON_IsBool(fail_safe)) {
            out_status->climate.fail_safe_active = cJSON_IsTrue(fail_safe);
        }
        if (cJSON_IsNumber(temp_sp)) {
            out_status->climate.temp_setpoint_c = temp_sp->valuedouble;
        }
        if (cJSON_IsNumber(hum_sp)) {
            out_status->climate.humidity_setpoint_pct = hum_sp->valuedouble;
        }
        if (cJSON_IsNumber(uvi_target)) {
            out_status->climate.uvi_target = uvi_target->valuedouble;
        }
        if (cJSON_IsBool(uvi_valid)) {
            out_status->climate.uvi_valid = cJSON_IsTrue(uvi_valid);
        }
        if (cJSON_IsNumber(uvi_measured)) {
            out_status->climate.uvi_measured = uvi_measured->valuedouble;
        }
        if (cJSON_IsNumber(uvi_error)) {
            out_status->climate.uvi_error = uvi_error->valuedouble;
        }
        if (cJSON_IsNumber(irradiance)) {
            out_status->climate.irradiance_uW_cm2 = irradiance->valuedouble;
        }
    }

    cJSON *alarms = cJSON_GetObjectItem(root, "alarms");
    if (cJSON_IsObject(alarms)) {
        cJSON *muted = cJSON_GetObjectItem(alarms, "muted");
        if (cJSON_IsBool(muted)) {
            out_status->alarm_muted = cJSON_IsTrue(muted);
        }
    }

    cJSON *calibration = cJSON_GetObjectItem(root, "calibration");
    if (cJSON_IsObject(calibration)) {
        cJSON *k = cJSON_GetObjectItem(calibration, "k");
        cJSON *uvi_max = cJSON_GetObjectItem(calibration, "uvi_max");
        if (cJSON_IsNumber(k) && cJSON_IsNumber(uvi_max)) {
            out_status->uvb_calibration.valid = true;
            out_status->uvb_calibration.k = k->valuedouble;
            out_status->uvb_calibration.uvi_max = uvi_max->valuedouble;
        }
    }

    out_status->valid = true;
    cJSON_Delete(root);
}

static esp_err_t parse_species_json(const char *json, terrarium_species_catalog_t *out_catalog)
{
    if (!json || !out_catalog) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_catalog, 0, sizeof(*out_catalog));
    size_t index = 0;
    cJSON *builtin = cJSON_GetObjectItem(root, "builtin");
    if (cJSON_IsArray(builtin)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, builtin)
        {
            if (index >= NETWORK_SPECIES_MAX_ENTRIES) {
                break;
            }
            cJSON *key = cJSON_GetObjectItem(item, "key");
            cJSON *labels = cJSON_GetObjectItem(item, "labels");
            if (!cJSON_IsString(key) || !cJSON_IsObject(labels)) {
                continue;
            }
            terrarium_species_entry_t *entry = &out_catalog->entries[index++];
            strlcpy(entry->key, key->valuestring, sizeof(entry->key));
            cJSON *fr = cJSON_GetObjectItem(labels, "fr");
            cJSON *en = cJSON_GetObjectItem(labels, "en");
            cJSON *es = cJSON_GetObjectItem(labels, "es");
            if (cJSON_IsString(fr)) {
                strlcpy(entry->label_fr, fr->valuestring, sizeof(entry->label_fr));
            }
            if (cJSON_IsString(en)) {
                strlcpy(entry->label_en, en->valuestring, sizeof(entry->label_en));
            }
            if (cJSON_IsString(es)) {
                strlcpy(entry->label_es, es->valuestring, sizeof(entry->label_es));
            }
            entry->custom = false;
        }
    }
    cJSON *custom = cJSON_GetObjectItem(root, "custom");
    if (cJSON_IsArray(custom)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, custom)
        {
            if (index >= NETWORK_SPECIES_MAX_ENTRIES) {
                break;
            }
            cJSON *key = cJSON_GetObjectItem(item, "key");
            cJSON *name = cJSON_GetObjectItem(item, "name");
            if (!cJSON_IsString(key) || !cJSON_IsString(name)) {
                continue;
            }
            terrarium_species_entry_t *entry = &out_catalog->entries[index++];
            strlcpy(entry->key, key->valuestring, sizeof(entry->key));
            strlcpy(entry->label_fr, name->valuestring, sizeof(entry->label_fr));
            strlcpy(entry->label_en, name->valuestring, sizeof(entry->label_en));
            strlcpy(entry->label_es, name->valuestring, sizeof(entry->label_es));
            entry->custom = true;
        }
    }
    out_catalog->count = index;
    cJSON *active = cJSON_GetObjectItem(root, "active_key");
    if (cJSON_IsString(active)) {
        strlcpy(out_catalog->active_key, active->valuestring, sizeof(out_catalog->active_key));
    }
    cJSON_Delete(root);
    return ESP_OK;
}
