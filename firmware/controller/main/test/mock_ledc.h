#pragma once

#include <stdbool.h>

void mock_ledc_reset(void);
bool mock_ledc_timer_configured(void);
bool mock_ledc_channel_configured(int channel);
int mock_ledc_get_set_duty_errors(void);
