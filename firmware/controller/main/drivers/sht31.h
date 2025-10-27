#pragma once
#include "driver/i2c.h"
#include "esp_err.h"
esp_err_t sht31_read(i2c_port_t port, uint8_t addr, float* t_c, float* rh);
