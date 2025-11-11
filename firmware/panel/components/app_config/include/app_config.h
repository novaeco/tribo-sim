#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_NAMESPACE "panelcfg"
#define APP_CONFIG_BLOB_KEY "cfg"

#define APP_CONFIG_DEFAULT_SSID        "terrarium-s3"
#define APP_CONFIG_DEFAULT_PASSWORD    "terrarium123"
#define APP_CONFIG_DEFAULT_HOST        "192.168.4.1"
#define APP_CONFIG_DEFAULT_PORT        443
#define APP_CONFIG_DEFAULT_USE_TLS     true
#define APP_CONFIG_DEFAULT_LANGUAGE          "fr"
#define APP_CONFIG_DEFAULT_SPECIES_KEY       "builtin:pogona_vitticeps"
#define APP_CONFIG_DEFAULT_AUTO_PROVISION_CA true

#define APP_CONFIG_MAX_SSID_LEN        32
#define APP_CONFIG_MAX_PASSWORD_LEN    64
#define APP_CONFIG_MAX_HOST_LEN        63

typedef struct {
    char ssid[APP_CONFIG_MAX_SSID_LEN + 1];
    char password[APP_CONFIG_MAX_PASSWORD_LEN + 1];
    char controller_host[APP_CONFIG_MAX_HOST_LEN + 1];
    uint16_t controller_port;
    bool use_tls;
    char language[3];
    char species_key[48];
    bool auto_provision_root_ca;
} app_config_t;

typedef struct {
    esp_err_t (*open)(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
    void (*close)(nvs_handle_t handle);
    esp_err_t (*get_blob)(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
    esp_err_t (*set_blob)(nvs_handle_t handle, const char *key, const void *value, size_t length);
    esp_err_t (*commit)(nvs_handle_t handle);
} app_config_nvs_ops_t;

void app_config_get_defaults(app_config_t *cfg);
esp_err_t app_config_load(app_config_t *cfg);
esp_err_t app_config_save(const app_config_t *cfg);
void app_config_use_custom_nvs_ops(const app_config_nvs_ops_t *ops);

#ifdef __cplusplus
}
#endif
