#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
  TERRA_SENSOR_DS18B20_EXT1 = 0,
  TERRA_SENSOR_DS18B20_EXT2 = 1,
  TERRA_SENSOR_SHT31        = 2,
  TERRA_SENSOR_SHT21        = 3,
  TERRA_SENSOR_BME280       = 4,
  TERRA_SENSOR_COUNT
} terra_sensor_slot_t;

typedef struct {
  bool      present;
  bool      error;
  int64_t   last_valid_timestamp_ms;
  esp_err_t last_error;
} terra_sensor_status_t;

typedef enum {
  TERRA_SENSOR_FILTER_NONE = 0,
  TERRA_SENSOR_FILTER_EMA,
  TERRA_SENSOR_FILTER_MEDIAN3,
} terra_sensor_filter_mode_t;

#define TERRA_SENSOR_FAULT_T1    (1u << TERRA_SENSOR_DS18B20_EXT1)
#define TERRA_SENSOR_FAULT_T2    (1u << TERRA_SENSOR_DS18B20_EXT2)
#define TERRA_SENSOR_FAULT_SHT31 (1u << TERRA_SENSOR_SHT31)
#define TERRA_SENSOR_FAULT_SHT21 (1u << TERRA_SENSOR_SHT21)
#define TERRA_SENSOR_FAULT_BME   (1u << TERRA_SENSOR_BME280)

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
  // Filtered aggregates
  float temp_filtered_c;
  float humidity_filtered_pct;
  bool  temp_filtered_valid;
  bool  humidity_filtered_valid;
  uint32_t fault_mask;
  terra_sensor_status_t status[TERRA_SENSOR_COUNT];
} terra_sensors_t;

extern const char * const terra_sensor_names[TERRA_SENSOR_COUNT];

void sensors_init(void);
void sensors_configure_filter(terra_sensor_filter_mode_t mode, float ema_alpha);
terra_sensor_filter_mode_t sensors_filter_mode(void);
float sensors_filter_alpha(void);
uint32_t sensors_read(terra_sensors_t* out);
