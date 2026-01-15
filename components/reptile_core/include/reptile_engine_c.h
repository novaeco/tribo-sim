/**
 * @file reptile_engine_c.h
 * @brief C interface for the Reptile Simulation Engine
 */

#ifndef REPTILE_ENGINE_C_H
#define REPTILE_ENGINE_C_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void reptile_engine_init(void);
void reptile_engine_tick(float delta_time);

uint32_t reptile_engine_get_day(void);
float reptile_engine_get_time_hours(void);
int reptile_engine_get_reptile_count(void);
int reptile_engine_get_terrarium_count(void);
void reptile_engine_set_heater(uint32_t terrarium_id, bool on);
void reptile_engine_set_light(uint32_t terrarium_id, bool on);
void reptile_engine_set_mister(uint32_t terrarium_id, bool on);
void reptile_engine_feed_animal(uint32_t reptile_id);
void reptile_engine_clean_terrarium(uint32_t terrarium_id);
float reptile_engine_get_terrarium_temp(uint32_t terrarium_id);
float reptile_engine_get_terrarium_humidity(uint32_t terrarium_id);
float reptile_engine_get_terrarium_waste(uint32_t terrarium_id);
bool reptile_engine_get_heater_state(uint32_t terrarium_id);
bool reptile_engine_get_light_state(uint32_t terrarium_id);
bool reptile_engine_get_mister_state(uint32_t terrarium_id);
float reptile_engine_get_reptile_stress(uint32_t reptile_id);
float reptile_engine_get_reptile_weight(uint32_t reptile_id);
bool reptile_engine_is_reptile_hungry(uint32_t reptile_id);
bool reptile_engine_is_reptile_healthy(uint32_t reptile_id);
bool reptile_engine_save_game(const char *filepath);
bool reptile_engine_load_game(const char *filepath);
uint32_t reptile_engine_add_reptile(const char *name, const char *species);
uint32_t reptile_engine_add_terrarium(float width, float height, float depth);

#ifdef __cplusplus
}
#endif

#endif // REPTILE_ENGINE_C_H
