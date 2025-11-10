#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "drivers/climate.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char        code[6];
    const char *label;
} species_label_entry_t;

typedef struct {
    const char *habitat;
    const char *uv_index_category;
    const char *season_cycle;
    float        uv_index_peak;
} species_profile_metadata_t;

typedef struct {
    const char                *key;
    const species_label_entry_t *labels;
    size_t                     label_count;
    species_profile_metadata_t metadata;
    climate_schedule_t         schedule;
} species_profile_t;

typedef struct {
    char              key[32];
    char              name[64];
    climate_schedule_t schedule;
    float             uv_index_peak;
    char              habitat[96];
    char              uv_index_category[32];
    char              season_cycle[48];
} species_custom_profile_t;

esp_err_t species_profiles_init(void);
size_t    species_profiles_builtin_count(void);
const species_profile_t *species_profiles_builtin(size_t index);
size_t    species_profiles_custom_count(void);
esp_err_t species_profiles_custom_get(size_t index, species_custom_profile_t *out);
esp_err_t species_profiles_apply(const char *key);
esp_err_t species_profiles_get_active_key(char *out, size_t len);
const char *species_profiles_label_for_locale(const species_profile_t *profile, const char *lang, const char *fallback_lang);
esp_err_t species_profiles_save_custom(const char *name,
                                       const climate_schedule_t *schedule,
                                       const species_profile_metadata_t *metadata,
                                       char *out_key,
                                       size_t out_len);
esp_err_t species_profiles_delete_custom(const char *key);
esp_err_t species_profiles_export_secure(uint8_t **payload, size_t *payload_len, uint8_t nonce[16], uint8_t signature[32]);
esp_err_t species_profiles_import_secure(const uint8_t *payload, size_t payload_len, const uint8_t nonce[16], const uint8_t signature[32]);
void     species_profiles_reset(void);

#ifdef __cplusplus
}
#endif
