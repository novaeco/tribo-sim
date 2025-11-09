#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "drivers/climate.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;
    const char *label_fr;
    const char *label_en;
    const char *label_es;
    const char *habitat;
    climate_schedule_t schedule;
} species_profile_t;

typedef struct {
    char key[32];
    char name[64];
    climate_schedule_t schedule;
} species_custom_profile_t;

esp_err_t species_profiles_init(void);
size_t    species_profiles_builtin_count(void);
const species_profile_t *species_profiles_builtin(size_t index);
size_t    species_profiles_custom_count(void);
esp_err_t species_profiles_custom_get(size_t index, species_custom_profile_t *out);
esp_err_t species_profiles_apply(const char *key);
esp_err_t species_profiles_get_active_key(char *out, size_t len);
esp_err_t species_profiles_save_custom(const char *name, const climate_schedule_t *schedule, char *out_key, size_t out_len);
esp_err_t species_profiles_delete_custom(const char *key);

#ifdef __cplusplus
}
#endif
