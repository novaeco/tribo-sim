#pragma once
#include "driver/i2c_master.h"
typedef struct { int year, month, day, hour, min, sec; } ds3231_time_t;
esp_err_t ds3231_get_time(i2c_port_t port, uint8_t addr, ds3231_time_t* out);
