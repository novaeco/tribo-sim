#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "app_config.h"

esp_err_t network_http_event_handler_cb(esp_http_client_event_t *evt);
#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_MAX_SPECIES_KEY    48
#define NETWORK_MAX_SPECIES_LABEL  64
#define NETWORK_SPECIES_MAX_ENTRIES 24

typedef struct {
    bool valid;
    float temperature_c;
    float humidity_percent;
    float pressure_hpa;
    float uvi;
    float irradiance_uW_cm2;
} terrarium_env_snapshot_t;

typedef struct {
    bool valid;
    uint16_t cct_day;
    uint16_t cct_warm;
    uint16_t uva_set;
    uint16_t uva_clamp;
    uint16_t uvb_set;
    uint16_t uvb_clamp;
    uint16_t uvb_period_s;
    uint16_t uvb_duty_pm;
    uint8_t sky_mode;
    float fan_pwm_percent;
} terrarium_light_state_t;

typedef struct {
    bool valid;
    float k;
    float uvi_max;
} terrarium_uvb_calibration_t;

typedef struct {
    bool valid;
    uint8_t status;
    uint8_t flags;
    float heatsink_c;
    float uvi;
    float irradiance_uW_cm2;
    bool uvi_fault;
} terrarium_dome_snapshot_t;

typedef struct {
    bool valid;
    bool heater_on;
    bool lights_on;
    bool fail_safe_active;
    float temp_setpoint_c;
    float humidity_setpoint_pct;
    float uvi_target;
    bool uvi_valid;
    float uvi_measured;
    float uvi_error;
    float irradiance_uW_cm2;
} terrarium_climate_snapshot_t;

typedef struct {
    uint16_t cct_day;
    uint16_t cct_warm;
    uint16_t uva_set;
    uint16_t uva_clamp;
    uint16_t uvb_set;
    uint16_t uvb_clamp;
    uint16_t uvb_period_s;
    uint16_t uvb_duty_pm;
    uint8_t sky;
} terrarium_light_command_t;

typedef struct {
    float k;
    float uvi_max;
} terrarium_uvb_calibration_command_t;

typedef struct {
    bool valid;
    terrarium_env_snapshot_t env;
    terrarium_light_state_t light;
    terrarium_uvb_calibration_t uvb_calibration;
    terrarium_dome_snapshot_t dome;
    terrarium_climate_snapshot_t climate;
    bool alarm_muted;
    uint64_t timestamp_ms;
    char summary[128];
} terrarium_status_t;

typedef struct {
    char key[NETWORK_MAX_SPECIES_KEY];
    char label_fr[NETWORK_MAX_SPECIES_LABEL];
    char label_en[NETWORK_MAX_SPECIES_LABEL];
    char label_es[NETWORK_MAX_SPECIES_LABEL];
    bool custom;
} terrarium_species_entry_t;

typedef struct {
    terrarium_species_entry_t entries[NETWORK_SPECIES_MAX_ENTRIES];
    size_t count;
    char active_key[NETWORK_MAX_SPECIES_KEY];
} terrarium_species_catalog_t;

typedef struct {
    char *buffer;
    int length;
    int capacity;
} network_http_response_buffer_t;

typedef void (*network_status_cb_t)(const terrarium_status_t *status, void *ctx);
typedef void (*network_error_cb_t)(esp_err_t err, const char *message, void *ctx);
typedef void (*network_species_cb_t)(const terrarium_species_catalog_t *catalog, void *ctx);

typedef struct {
    bool available;
    bool custom;
    size_t length;
} network_root_ca_status_t;

typedef struct {
    esp_err_t (*wifi_init)(const wifi_init_config_t *config);
    esp_err_t (*wifi_set_mode)(wifi_mode_t mode);
    esp_err_t (*wifi_set_config)(wifi_interface_t interface, wifi_config_t *config);
    esp_err_t (*wifi_start)(void);
    esp_err_t (*wifi_stop)(void);
    esp_err_t (*wifi_deinit)(void);
    esp_err_t (*wifi_connect)(void);
    esp_err_t (*wifi_disconnect)(void);
    BaseType_t (*task_create_pinned_to_core)(TaskFunction_t task,
                                             const char *name,
                                             const uint32_t stack_depth,
                                             void *params,
                                             UBaseType_t priority,
                                             TaskHandle_t *out_handle,
                                             const BaseType_t core_id);
    void (*task_delete)(TaskHandle_t handle);
    esp_err_t (*timer_create)(const esp_timer_create_args_t *args, esp_timer_handle_t *out_timer);
    esp_err_t (*timer_stop)(esp_timer_handle_t timer);
    esp_err_t (*timer_delete)(esp_timer_handle_t timer);
    esp_err_t (*event_handler_register)(esp_event_base_t event_base,
                                        int32_t event_id,
                                        esp_event_handler_t handler,
                                        void *handler_arg,
                                        esp_event_handler_instance_t *instance_out);
    esp_err_t (*event_handler_unregister)(esp_event_base_t event_base,
                                          int32_t event_id,
                                          esp_event_handler_instance_t instance);
} network_manager_runtime_ops_t;

esp_err_t network_manager_init(const app_config_t *config);
esp_err_t network_manager_start(const app_config_t *config);
esp_err_t network_manager_stop(void);

esp_err_t network_manager_register_status_callback(network_status_cb_t cb, void *ctx);
void      network_manager_register_error_callback(network_error_cb_t cb, void *ctx);
void      network_manager_register_species_callback(network_species_cb_t cb, void *ctx);

void network_manager_use_custom_runtime_ops(const network_manager_runtime_ops_t *ops);

esp_err_t network_manager_post_light(const terrarium_light_command_t *cmd);
esp_err_t network_manager_fetch_calibration(void);
esp_err_t network_manager_post_calibration(const terrarium_uvb_calibration_command_t *cmd);
esp_err_t network_manager_set_alarm_mute(bool mute);
esp_err_t network_manager_request_species_catalog(void);
esp_err_t network_manager_apply_species(const char *key);
esp_err_t network_manager_upload_controller_ota(const char *path);
esp_err_t network_manager_upload_dome_ota(const char *path);

esp_err_t network_manager_import_root_ca_from_file(const char *path);
esp_err_t network_manager_import_root_ca_from_buffer(const uint8_t *data, size_t len);
esp_err_t network_manager_auto_provision_root_ca(void);
void      network_manager_get_root_ca_status(network_root_ca_status_t *status);

const terrarium_status_t *network_manager_get_cached_status(void);
const terrarium_species_catalog_t *network_manager_get_cached_species(void);

void network_manager_prepare_http_client_config(const app_config_t *cfg,
                                                const char *path,
                                                esp_http_client_method_t method,
                                                network_http_response_buffer_t *resp,
                                                esp_http_client_config_t *out);

#ifdef __cplusplus
}
#endif

