#include "app_config.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app_cfg";

void app_config_get_defaults(app_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    strlcpy(cfg->ssid, APP_CONFIG_DEFAULT_SSID, sizeof(cfg->ssid));
    strlcpy(cfg->password, APP_CONFIG_DEFAULT_PASSWORD, sizeof(cfg->password));
    strlcpy(cfg->controller_host, APP_CONFIG_DEFAULT_HOST, sizeof(cfg->controller_host));
    cfg->controller_port = APP_CONFIG_DEFAULT_PORT;
    cfg->use_tls = APP_CONFIG_DEFAULT_USE_TLS;
    strlcpy(cfg->language, APP_CONFIG_DEFAULT_LANGUAGE, sizeof(cfg->language));
    strlcpy(cfg->species_key, APP_CONFIG_DEFAULT_SPECIES_KEY, sizeof(cfg->species_key));
    cfg->auto_provision_root_ca = APP_CONFIG_DEFAULT_AUTO_PROVISION_CA;
}

esp_err_t app_config_load(app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    app_config_get_defaults(cfg);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_CONFIG_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No configuration stored yet (%s)", esp_err_to_name(err));
        return err;
    }

    size_t required = 0;
    err = nvs_get_blob(nvs, APP_CONFIG_BLOB_KEY, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Configuration blob not found; using defaults");
        nvs_close(nvs);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to query configuration size (%s)", esp_err_to_name(err));
        nvs_close(nvs);
        app_config_get_defaults(cfg);
        return err;
    }

    esp_err_t load_err = ESP_OK;
    if (required == sizeof(app_config_t)) {
        load_err = nvs_get_blob(nvs, APP_CONFIG_BLOB_KEY, cfg, &required);
    } else {
        typedef struct {
            char ssid[APP_CONFIG_MAX_SSID_LEN + 1];
            char password[APP_CONFIG_MAX_PASSWORD_LEN + 1];
            char controller_host[APP_CONFIG_MAX_HOST_LEN + 1];
            uint16_t controller_port;
            bool use_tls;
            char language[3];
            char species_key[48];
        } app_config_legacy_t;

        if (required == sizeof(app_config_legacy_t)) {
            app_config_legacy_t legacy = {0};
            load_err = nvs_get_blob(nvs, APP_CONFIG_BLOB_KEY, &legacy, &required);
            if (load_err == ESP_OK) {
                strlcpy(cfg->ssid, legacy.ssid, sizeof(cfg->ssid));
                strlcpy(cfg->password, legacy.password, sizeof(cfg->password));
                strlcpy(cfg->controller_host, legacy.controller_host, sizeof(cfg->controller_host));
                cfg->controller_port = legacy.controller_port;
                cfg->use_tls = legacy.use_tls;
                strlcpy(cfg->language, legacy.language, sizeof(cfg->language));
                strlcpy(cfg->species_key, legacy.species_key, sizeof(cfg->species_key));
                cfg->auto_provision_root_ca = APP_CONFIG_DEFAULT_AUTO_PROVISION_CA;
            }
        } else {
            ESP_LOGW(TAG, "Unknown configuration blob size %zu; resetting to defaults", required);
            load_err = ESP_ERR_NVS_INVALID_LENGTH;
        }
    }
    nvs_close(nvs);
    if (load_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load configuration (%s)", esp_err_to_name(load_err));
        app_config_get_defaults(cfg);
        return load_err;
    }
    return ESP_OK;
}

esp_err_t app_config_save(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_CONFIG_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, APP_CONFIG_BLOB_KEY, cfg, sizeof(app_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration (%s)", esp_err_to_name(err));
    }
    return err;
}
