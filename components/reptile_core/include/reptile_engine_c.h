/**
 * @file reptile_engine_c.h
 * @brief C interface for the Reptile Simulation Engine
 */

#ifndef REPTILE_ENGINE_C_H
#define REPTILE_ENGINE_C_H

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

#ifdef __cplusplus
}
#endif

#endif // REPTILE_ENGINE_C_H
