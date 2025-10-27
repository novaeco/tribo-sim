#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "drivers/sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temp_c;
    float humidity_pct;
    float temp_hysteresis_c;
    float humidity_hysteresis_pct;
} climate_profile_t;

typedef struct {
    int day_start_minute;   // minutes from 00:00
    int night_start_minute; // minutes from 00:00
    climate_profile_t day;
    climate_profile_t night;
    float day_uvi_max;
    float night_uvi_max;
} climate_schedule_t;

typedef struct {
    terra_sensors_t sensors;
    float temp_drift_c;
    float humidity_drift_pct;
    int64_t timestamp_ms;
} climate_measurement_t;

typedef struct {
    bool is_day;
    float temp_setpoint_c;
    float humidity_setpoint_pct;
    float temp_hysteresis_c;
    float humidity_hysteresis_pct;
    float uvi_target;
    bool heater_on;
    bool lights_on;
    uint8_t fan_pwm_percent;
    float temp_error_c;
    float humidity_error_pct;
} climate_state_t;

esp_err_t climate_init(void);
esp_err_t climate_get_schedule(climate_schedule_t *out);
esp_err_t climate_update_targets(const climate_schedule_t *schedule);
void      climate_tick(const terra_sensors_t *sensors, int minute_of_day, climate_state_t *out_state);
bool      climate_get_state(climate_state_t *out_state);

SemaphoreHandle_t climate_measurement_mutex(void);
void      climate_measurement_set_locked(const climate_measurement_t *m);
bool      climate_measurement_get(climate_measurement_t *out);

#ifdef __cplusplus
}
#endif

