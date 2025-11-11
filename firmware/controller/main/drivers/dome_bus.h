#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t dome_bus_lock(void);
void      dome_bus_unlock(void);
esp_err_t dome_bus_select(uint8_t channel_mask);
esp_err_t dome_bus_read(uint8_t reg, uint8_t* b, size_t n);
esp_err_t dome_bus_write(uint8_t reg, const uint8_t* b, size_t n);
bool      dome_bus_is_degraded(void);
void      dome_bus_clear_degraded(void);
