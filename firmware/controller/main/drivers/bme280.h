#pragma once
#include "driver/i2c.h"
#include "esp_err.h"
typedef struct { float t_c, p_hpa, rh; } bme280_data_t;
esp_err_t bme280_init(i2c_port_t port, uint8_t addr);
esp_err_t bme280_read(i2c_port_t port, uint8_t addr, bme280_data_t* out);
