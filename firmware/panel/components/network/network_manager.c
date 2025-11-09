#include "network_manager.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "cJSON.h"

#define TAG "net"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define NETWORK_QUEUE_LENGTH 10
#define NETWORK_CMD_TIMEOUT_MS 2000
#define STATUS_POLL_INTERVAL_MS 2000

typedef enum {
    NETWORK_CMD_LIGHT_POST,
    NETWORK_CMD_CALIB_GET,
    NETWORK_CMD_CALIB_POST,
    NETWORK_CMD_MUTE_SET,
} network_command_type_t;

typedef struct {
    network_command_type_t type;
    union {
        terrarium_light_command_t light;
        terrarium_uvb_calibration_command_t calib;
        bool mute;
    } payload;
} network_command_t;

static app_config_t s_config;
static EventGroupHandle_t s_wifi_event_group;
static QueueHandle_t s_command_queue;
static terrarium_status_t s_status;
static network_status_cb_t s_status_cb;
static void *s_status_ctx;
static network_error_cb_t s_error_cb;
static void *s_error_ctx;
static TaskHandle_t s_network_task_handle;
static bool s_wifi_initialized;
static bool s_network_started;
static esp_event_handler_instance_t s_wifi_any_id_handle;
static esp_event_handler_instance_t s_ip_got_ip_handle;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void network_task(void *arg);
static esp_err_t network_fetch_status(void);
static esp_err_t network_fetch_calibration_internal(void);
static esp_err_t network_post_light_internal(const terrarium_light_command_t *cmd);
static esp_err_t network_post_calibration_internal(const terrarium_uvb_calibration_command_t *cmd);
static esp_err_t network_set_alarm_mute_internal(bool mute);
static void network_invoke_status_cb(void);
static void network_report_error(esp_err_t err, const char *msg);

static esp_err_t http_perform(const char *path, esp_http_client_method_t method, const char *payload, size_t payload_len, char **out_body, int *out_len);
static void parse_status_json(const char *json, terrarium_status_t *out_status);
static void parse_env_sensor(cJSON *parent, const char *name, terrarium_env_sensor_t *sensor);
static void parse_temp_sensor(cJSON *parent, const char *name, terrarium_temp_sensor_t *sensor);

esp_err_t network_manager_init(const app_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_network_started) {
        bool credentials_changed = (strncmp(s_config.ssid, config->ssid, sizeof(s_config.ssid)) != 0) ||
                                   (strncmp(s_config.password, config->password, sizeof(s_config.password)) != 0);
        s_config = *config;
        if (credentials_changed) {
            wifi_config_t wifi_cfg = {0};
            strlcpy((char *)wifi_cfg.sta.ssid, s_config.ssid, sizeof(wifi_cfg.sta.ssid));
            strlcpy((char *)wifi_cfg.sta.password, s_config.password, sizeof(wifi_cfg.sta.password));
            wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            wifi_cfg.sta.sae_pk_mode = WPA3_SAE_PK_MODE_AUTOMATIC;
            ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "Failed to disconnect before reconfig");
            ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "Failed to update Wi-Fi config");
            ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Failed to reconnect Wi-Fi");
        }
        return ESP_OK;
    }

    s_config = *config;
    memset(&s_status, 0, sizeof(s_status));

    if (!s_wifi_initialized) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init Wi-Fi");
        s_wifi_initialized = true;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create Wi-Fi event group");

    s_command_queue = xQueueCreate(NETWORK_QUEUE_LENGTH, sizeof(network_command_t));
    if (!s_command_queue) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_any_id_handle);
    if (err != ESP_OK) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_ip_got_ip_handle);
    if (err != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_any_id_handle);
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return err;
    }

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, s_config.ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, s_config.password, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_cfg.sta.sae_pk_mode = WPA3_SAE_PK_MODE_AUTOMATIC;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set Wi-Fi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "Failed to set Wi-Fi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Failed to connect Wi-Fi");

    BaseType_t task_ok = xTaskCreatePinnedToCore(network_task, "net_task", 8192, NULL, 5, &s_network_task_handle, 0);
    if (task_ok != pdPASS) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_got_ip_handle);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_any_id_handle);
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_network_started = true;
    return ESP_OK;
}

esp_err_t network_manager_register_status_callback(network_status_cb_t cb, void *ctx)
{
    s_status_cb = cb;
    s_status_ctx = ctx;
    if (s_status.valid && s_status_cb) {
        s_status_cb(&s_status, s_status_ctx);
    }
    return ESP_OK;
}

void network_manager_register_error_callback(network_error_cb_t cb, void *ctx)
{
    s_error_cb = cb;
    s_error_ctx = ctx;
}

esp_err_t network_manager_post_light(const terrarium_light_command_t *cmd)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {
        .type = NETWORK_CMD_LIGHT_POST,
        .payload.light = *cmd,
    };
    if (xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_fetch_calibration(void)
{
    if (!s_command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {
        .type = NETWORK_CMD_CALIB_GET,
    };
    if (xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_post_calibration(const terrarium_uvb_calibration_command_t *cmd)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {
        .type = NETWORK_CMD_CALIB_POST,
        .payload.calib = *cmd,
    };
    if (xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_set_alarm_mute(bool mute)
{
    if (!s_command_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    network_command_t command = {
        .type = NETWORK_CMD_MUTE_SET,
        .payload.mute = mute,
    };
    if (xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(NETWORK_CMD_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

const terrarium_status_t *network_manager_get_cached_status(void)
{
    return s_status.valid ? &s_status : NULL;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (!s_wifi_event_group) {
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void network_task(void *arg)
{
    (void)arg;
    TickType_t last_poll = xTaskGetTickCount() - pdMS_TO_TICKS(STATUS_POLL_INTERVAL_MS);
    while (1) {
        if (!s_wifi_event_group) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(100));
        if (!(bits & WIFI_CONNECTED_BIT)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const TickType_t now = xTaskGetTickCount();
        if (pdTICKS_TO_MS(now - last_poll) >= STATUS_POLL_INTERVAL_MS) {
            esp_err_t err = network_fetch_status();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to fetch status (%s)", esp_err_to_name(err));
            }
            last_poll = now;
        }

        network_command_t command;
        if (xQueueReceive(s_command_queue, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
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
            default:
                break;
            }
            if (err != ESP_OK) {
                network_report_error(err, "Command failed");
            } else if (command.type == NETWORK_CMD_LIGHT_POST || command.type == NETWORK_CMD_MUTE_SET) {
                network_fetch_status();
            }
        }
    }
}

static void network_report_error(esp_err_t err, const char *msg)
{
    if (s_error_cb) {
        s_error_cb(err, msg, s_error_ctx);
    }
}

static void network_invoke_status_cb(void)
{
    if (s_status_cb) {
        s_status_cb(&s_status, s_status_ctx);
    }
}

typedef struct {
    char *buffer;
    int length;
    int capacity;
} http_resp_buf_t;

static esp_err_t http_event_handler_cb(esp_http_client_event_t *evt)
{
    http_resp_buf_t *resp = (http_resp_buf_t *)evt->user_data;
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
            resp->capacity = resp->length + evt->data_len + 1;
            char *new_buf = realloc(resp->buffer, resp->capacity);
            if (!new_buf) {
                free(resp->buffer);
                resp->buffer = NULL;
                return ESP_ERR_NO_MEM;
            }
            resp->buffer = new_buf;
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

static esp_err_t http_perform(const char *path, esp_http_client_method_t method, const char *payload, size_t payload_len, char **out_body, int *out_len)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[128];
    const char *scheme = s_config.use_tls ? "https" : "http";
    snprintf(url, sizeof(url), "%s://%s:%u%s", scheme, s_config.controller_host, s_config.controller_port, path);

    http_resp_buf_t resp = {0};
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = method,
        .disable_auto_redirect = false,
        .user_data = &resp,
        .event_handler = http_event_handler_cb,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    if (payload && payload_len > 0) {
        esp_http_client_set_post_field(client, payload, payload_len);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code >= 400) {
            ESP_LOGW(TAG, "HTTP %d for %s", status_code, path);
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
            char *empty = malloc(1);
            if (!empty) {
                return ESP_ERR_NO_MEM;
            }
            empty[0] = '\0';
            *out_body = empty;
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
        network_report_error(err, "GET /api/status failed");
        return err;
    }
    parse_status_json(body, &s_status);
    free(body);
    if (!s_status.valid) {
        network_report_error(ESP_FAIL, "Status JSON invalide");
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
        network_report_error(err, "GET /api/calibrate/uvb failed");
        return err;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        network_report_error(ESP_FAIL, "Réponse calibration invalide");
        return ESP_FAIL;
    }
    terrarium_uvb_calibration_t calibration = {0};
    cJSON *k = cJSON_GetObjectItem(root, "k");
    cJSON *uvi = cJSON_GetObjectItem(root, "uvi_max");
    if (cJSON_IsNumber(k) && cJSON_IsNumber(uvi)) {
        calibration.valid = true;
        calibration.k = (float)k->valuedouble;
        calibration.uvi_max = (float)uvi->valuedouble;
    }
    cJSON_Delete(root);
    if (calibration.valid) {
        s_status.uvb_calibration = calibration;
        network_invoke_status_cb();
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t network_post_calibration_internal(const terrarium_uvb_calibration_command_t *cmd)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    if (!cJSON_AddNumberToObject(root, "k", cmd->k) || !cJSON_AddNumberToObject(root, "uvi_max", cmd->uvi_max)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = http_perform("/api/calibrate/uvb", HTTP_METHOD_POST, payload, strlen(payload), NULL, NULL);
    free(payload);
    if (err == ESP_OK) {
        s_status.uvb_calibration.valid = true;
        s_status.uvb_calibration.k = cmd->k;
        s_status.uvb_calibration.uvi_max = cmd->uvi_max;
        network_invoke_status_cb();
    }
    return err;
}

static esp_err_t network_post_light_internal(const terrarium_light_command_t *cmd)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *cct = cJSON_CreateObject();
    cJSON *uva = cJSON_CreateObject();
    cJSON *uvb = cJSON_CreateObject();
    if (!cct || !uva || !uvb) {
        if (cct) cJSON_Delete(cct);
        if (uva) cJSON_Delete(uva);
        if (uvb) cJSON_Delete(uvb);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    if (!cJSON_AddNumberToObject(cct, "day", cmd->cct_day) ||
        !cJSON_AddNumberToObject(cct, "warm", cmd->cct_warm) ||
        !cJSON_AddNumberToObject(uva, "set", cmd->uva) ||
        !cJSON_AddNumberToObject(uvb, "set", cmd->uvb) ||
        !cJSON_AddNumberToObject(uvb, "period_s", cmd->uvb_period_s) ||
        !cJSON_AddNumberToObject(uvb, "duty_pm", cmd->uvb_duty_pm) ||
        !cJSON_AddItemToObject(root, "cct", cct) ||
        !cJSON_AddItemToObject(root, "uva", uva) ||
        !cJSON_AddItemToObject(root, "uvb", uvb) ||
        !cJSON_AddNumberToObject(root, "sky", cmd->sky)) {
        cJSON_Delete(cct);
        cJSON_Delete(uva);
        cJSON_Delete(uvb);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = http_perform("/api/light/dome0", HTTP_METHOD_POST, payload, strlen(payload), NULL, NULL);
    free(payload);
    return err;
}

static esp_err_t network_set_alarm_mute_internal(bool mute)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    if (!cJSON_AddBoolToObject(root, "mute", mute)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = http_perform("/api/alarms/mute", HTTP_METHOD_POST, payload, strlen(payload), NULL, NULL);
    free(payload);
    if (err == ESP_OK) {
        s_status.dome.alarm_muted = mute;
        network_invoke_status_cb();
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

    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    if (cJSON_IsObject(sensors)) {
        parse_env_sensor(sensors, "sht31", &out_status->sensors.sht31);
        parse_env_sensor(sensors, "sht21", &out_status->sensors.sht21);
        parse_env_sensor(sensors, "bme280", &out_status->sensors.bme280);
        parse_temp_sensor(sensors, "ds18b20", &out_status->sensors.ds18b20);
        parse_env_sensor(sensors, "ambient", &out_status->sensors.ambient);
    }

    cJSON *dome = cJSON_GetObjectItem(root, "dome");
    if (cJSON_IsObject(dome)) {
        out_status->dome.valid = true;
        cJSON *status = cJSON_GetObjectItem(dome, "status");
        cJSON *interlock = cJSON_GetObjectItem(dome, "interlock");
        cJSON *therm_hard = cJSON_GetObjectItem(dome, "therm_hard");
        cJSON *bus_loss = cJSON_GetObjectItem(dome, "bus_loss");
        cJSON *alarm_mute = cJSON_GetObjectItem(dome, "alarm_muted");
        out_status->dome.status = cJSON_IsTrue(status);
        out_status->dome.interlock = cJSON_IsTrue(interlock);
        out_status->dome.therm_hard = cJSON_IsTrue(therm_hard);
        out_status->dome.bus_loss = cJSON_IsTrue(bus_loss);
        out_status->dome.alarm_muted = cJSON_IsTrue(alarm_mute);

        cJSON *cct = cJSON_GetObjectItem(dome, "cct");
        if (cJSON_IsObject(cct)) {
            cJSON *day = cJSON_GetObjectItem(cct, "day");
            cJSON *warm = cJSON_GetObjectItem(cct, "warm");
            if (cJSON_IsNumber(day)) {
                out_status->dome.cct_day = (uint16_t)day->valuedouble;
            }
            if (cJSON_IsNumber(warm)) {
                out_status->dome.cct_warm = (uint16_t)warm->valuedouble;
            }
        }
        cJSON *uva = cJSON_GetObjectItem(dome, "uva");
        if (cJSON_IsObject(uva)) {
            cJSON *set = cJSON_GetObjectItem(uva, "set");
            if (cJSON_IsNumber(set)) {
                out_status->dome.uva_set = (uint16_t)set->valuedouble;
            }
        }
        cJSON *uvb = cJSON_GetObjectItem(dome, "uvb");
        if (cJSON_IsObject(uvb)) {
            cJSON *set = cJSON_GetObjectItem(uvb, "set");
            cJSON *period = cJSON_GetObjectItem(uvb, "period_s");
            cJSON *duty = cJSON_GetObjectItem(uvb, "duty_pm");
            if (cJSON_IsNumber(set)) {
                out_status->dome.uvb_set = (uint16_t)set->valuedouble;
            }
            if (cJSON_IsNumber(period)) {
                out_status->dome.uvb_period_s = (uint16_t)period->valuedouble;
            }
            if (cJSON_IsNumber(duty)) {
                out_status->dome.uvb_duty_pm = (uint16_t)duty->valuedouble;
            }
        }
        cJSON *sky = cJSON_GetObjectItem(dome, "sky");
        if (cJSON_IsNumber(sky)) {
            out_status->dome.sky_mode = (uint8_t)sky->valuedouble;
        }
    }

    out_status->valid = true;
    cJSON_Delete(root);
}

static void parse_env_sensor(cJSON *parent, const char *name, terrarium_env_sensor_t *sensor)
{
    if (!sensor) {
        return;
    }
    memset(sensor, 0, sizeof(*sensor));
    if (!parent || !name) {
        return;
    }
    cJSON *node = cJSON_GetObjectItem(parent, name);
    if (!cJSON_IsObject(node)) {
        return;
    }
    cJSON *temp = cJSON_GetObjectItem(node, "temperature_c");
    cJSON *humid = cJSON_GetObjectItem(node, "humidity_percent");
    if (cJSON_IsNumber(temp)) {
        sensor->temperature_c = (float)temp->valuedouble;
        sensor->valid = true;
    }
    if (cJSON_IsNumber(humid)) {
        sensor->humidity_percent = (float)humid->valuedouble;
        sensor->valid = true;
    }
}

static void parse_temp_sensor(cJSON *parent, const char *name, terrarium_temp_sensor_t *sensor)
{
    if (!sensor) {
        return;
    }
    memset(sensor, 0, sizeof(*sensor));
    if (!parent || !name) {
        return;
    }
    cJSON *node = cJSON_GetObjectItem(parent, name);
    if (!cJSON_IsObject(node)) {
        return;
    }
    cJSON *temp = cJSON_GetObjectItem(node, "temperature_c");
    if (cJSON_IsNumber(temp)) {
        sensor->temperature_c = (float)temp->valuedouble;
        sensor->valid = true;
    }
}
