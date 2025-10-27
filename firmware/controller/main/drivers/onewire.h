#pragma once
#include "esp_err.h"
esp_err_t ow_init(int gpio);
esp_err_t ow_read_ds18b20_celsius(int gpio, float* out_c);
