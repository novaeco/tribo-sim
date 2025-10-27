#pragma once
#include "esp_err.h"
esp_err_t ledc_cc_init(void);
esp_err_t ledc_cc_set(int ch, int permille);
