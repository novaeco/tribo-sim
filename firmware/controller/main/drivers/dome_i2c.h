#pragma once
#include "driver/i2c.h"
esp_err_t dome_read_reg(i2c_port_t port, uint8_t addr, uint8_t reg, uint8_t* data, size_t len);
esp_err_t dome_write_reg(i2c_port_t port, uint8_t addr, uint8_t reg, const uint8_t* data, size_t len);
