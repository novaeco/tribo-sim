#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
typedef struct {
    float t_c;
    float p_hpa;
    float rh;
} bme280_data_t;

esp_err_t bme280_init(i2c_port_t port, uint8_t addr);
esp_err_t bme280_read(i2c_port_t port, uint8_t addr, bme280_data_t* out);

float bme280_compensate_temperature(int32_t adc_T);
float bme280_compensate_pressure(int32_t adc_P);
float bme280_compensate_humidity(int32_t adc_H);
