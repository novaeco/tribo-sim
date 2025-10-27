#pragma once
#include "driver/i2c.h"
#include <stddef.h>
esp_err_t i2c_slave_if_init(i2c_port_t port, int sda, int scl, uint8_t addr);
int       i2c_slave_if_read(uint8_t* buf, size_t maxlen, TickType_t to);
int       i2c_slave_if_write(const uint8_t* buf, size_t len, TickType_t to);
