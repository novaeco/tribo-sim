#pragma once
#include <stdbool.h>
typedef struct {
  // DS18B20
  float t1_c, t2_c;
  bool  t1_present, t2_present;
  // SHT31
  float sht31_t_c, sht31_rh;
  bool  sht31_present;
  // SHT21/HTU21
  float sht21_t_c, sht21_rh;
  bool  sht21_present;
  // BME280
  float bme_t_c, bme_rh, bme_p_hpa;
  bool  bme_present;
} terra_sensors_t;

void sensors_init(void);
int  sensors_read(terra_sensors_t* out);
