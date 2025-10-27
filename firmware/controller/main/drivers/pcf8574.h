#pragma once
#include "driver/i2c.h"
esp_err_t pcf8574_write(i2c_port_t port, uint8_t addr, uint8_t value);
