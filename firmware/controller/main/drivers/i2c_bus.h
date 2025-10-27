#pragma once
#include "driver/i2c.h"
esp_err_t i2c_bus_init(i2c_port_t port, int sda, int scl, uint32_t freq);
