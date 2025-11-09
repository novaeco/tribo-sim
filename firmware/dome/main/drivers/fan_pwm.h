#pragma once

#include <stdbool.h>
#include <stdint.h>

void fan_init(int pwm_gpio);
void fan_set_percent(float percent);
uint16_t fan_get_raw_pwm(void);
bool fan_is_running(void);
