#pragma once
#include "driver/i2c_master.h"

esp_err_t i2c_bus_init(i2c_port_t port, int sda, int scl, uint32_t freq);
i2c_master_bus_handle_t i2c_bus_get_handle(void);
uint32_t i2c_bus_get_frequency_hz(void);
