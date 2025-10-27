#pragma once
#include "esp_err.h"
esp_err_t calib_init(void);
esp_err_t calib_set_uvb(float duty_pm, float uvi_meas);
esp_err_t calib_get_uvb(float* k_out, float* uvi_max_out);
esp_err_t calib_set_uvb_uvi_max(float uvi_max);
int       uvb_duty_from_uvi(float uvi_target, float* duty_pm_out); // returns 0 ok
