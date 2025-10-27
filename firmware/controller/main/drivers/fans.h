#pragma once
#include <stdint.h>
#include "esp_err.h"

void fans_init(void);
esp_err_t fans_set_pwm(uint8_t channel, uint8_t duty_percent);
