#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    float temperature_c;
    float humidity_percent;
} terrarium_env_sensor_t;

typedef struct {
    bool valid;
    float temperature_c;
} terrarium_temp_sensor_t;

typedef struct {
    terrarium_env_sensor_t sht31;
    terrarium_env_sensor_t sht21;
    terrarium_env_sensor_t bme280;
    terrarium_temp_sensor_t ds18b20;
    terrarium_env_sensor_t ambient;
} terrarium_sensor_block_t;

typedef struct {
    bool valid;
    bool status;
    bool interlock;
    bool therm_hard;
    bool bus_loss;
    bool alarm_muted;
    uint16_t cct_day;
    uint16_t cct_warm;
    uint16_t uva_set;
    uint16_t uvb_set;
    uint16_t uvb_period_s;
    uint16_t uvb_duty_pm;
    uint8_t sky_mode;
} terrarium_dome_state_t;

typedef struct {
    bool valid;
    float k;
    float uvi_max;
} terrarium_uvb_calibration_t;

typedef struct {
    terrarium_sensor_block_t sensors;
    terrarium_dome_state_t dome;
    terrarium_uvb_calibration_t uvb_calibration;
    uint64_t timestamp_ms;
} terrarium_status_t;

typedef struct {
    uint16_t cct_day;
    uint16_t cct_warm;
    uint16_t uva;
    uint16_t uvb;
    uint16_t uvb_period_s;
    uint16_t uvb_duty_pm;
    uint8_t sky;
} terrarium_light_command_t;

typedef struct {
    float k;
    float uvi_max;
} terrarium_uvb_calibration_command_t;

typedef void (*network_status_cb_t)(const terrarium_status_t *status, void *ctx);

typedef void (*network_error_cb_t)(esp_err_t err, const char *message, void *ctx);

esp_err_t network_manager_init(const app_config_t *config);

esp_err_t network_manager_register_status_callback(network_status_cb_t cb, void *ctx);
void network_manager_register_error_callback(network_error_cb_t cb, void *ctx);

esp_err_t network_manager_post_light(const terrarium_light_command_t *cmd);
esp_err_t network_manager_fetch_calibration(void);
esp_err_t network_manager_post_calibration(const terrarium_uvb_calibration_command_t *cmd);
esp_err_t network_manager_set_alarm_mute(bool mute);

const terrarium_status_t *network_manager_get_cached_status(void);

#ifdef __cplusplus
}
#endif
