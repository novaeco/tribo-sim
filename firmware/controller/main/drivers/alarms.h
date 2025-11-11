#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>

void alarms_start(void);
esp_err_t alarms_init(void);
esp_err_t alarms_set_mute(bool muted);
bool alarms_get_mute(void);
bool alarms_wait_ready(TickType_t ticks_to_wait);
