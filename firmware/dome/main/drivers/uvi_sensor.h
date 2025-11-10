#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float irradiance_uW_cm2;
    float uvi;
    bool valid;
    bool fault;
    bool saturated;
    int64_t timestamp_us;
} uvi_sensor_measurement_t;

esp_err_t uvi_sensor_init(void);
esp_err_t uvi_sensor_poll(void);
bool      uvi_sensor_get(uvi_sensor_measurement_t *out);
void      uvi_sensor_force_reset(void);

#ifdef __cplusplus
}
#endif

