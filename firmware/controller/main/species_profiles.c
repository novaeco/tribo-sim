#include "species_profiles.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "SPECIES"

#define SPECIES_NAMESPACE    "species"
#define SPECIES_KEY_ACTIVE   "active_key"
#define SPECIES_KEY_CUSTOM   "custom_blob_v1"
#define SPECIES_CUSTOM_MAX   6

typedef struct {
    uint8_t used;
    char    key[32];
    char    name[64];
    climate_schedule_t schedule;
} custom_entry_t;

static const species_profile_t s_builtin[] = {
    {
        .key = "builtin:pogona_vitticeps",
        .label_fr = "Pogona vitticeps (Dragon barbu)",
        .label_en = "Pogona vitticeps (Bearded dragon)",
        .label_es = "Pogona vitticeps (Dragón barbudo)",
        .habitat = "Désert australien semi-aride",
        .schedule = {
            .day_start_minute = 7 * 60,
            .night_start_minute = 21 * 60,
            .day = { .temp_c = 35.0f, .humidity_pct = 35.0f, .temp_hysteresis_c = 2.0f, .humidity_hysteresis_pct = 10.0f },
            .night = { .temp_c = 22.0f, .humidity_pct = 45.0f, .temp_hysteresis_c = 1.5f, .humidity_hysteresis_pct = 12.0f },
            .day_uvi_max = 7.0f,
            .night_uvi_max = 0.5f,
        },
    },
    {
        .key = "builtin:furcifer_parsonii",
        .label_fr = "Furcifer parsonii (Caméléon de Parson)",
        .label_en = "Furcifer parsonii (Parson's chameleon)",
        .label_es = "Furcifer parsonii (Camaleón de Parson)",
        .habitat = "Forêt humide de Madagascar",
        .schedule = {
            .day_start_minute = 6 * 60,
            .night_start_minute = 19 * 60,
            .day = { .temp_c = 28.0f, .humidity_pct = 75.0f, .temp_hysteresis_c = 1.0f, .humidity_hysteresis_pct = 12.0f },
            .night = { .temp_c = 20.0f, .humidity_pct = 90.0f, .temp_hysteresis_c = 1.5f, .humidity_hysteresis_pct = 15.0f },
            .day_uvi_max = 3.5f,
            .night_uvi_max = 0.3f,
        },
    },
    {
        .key = "builtin:eublepharis_macularius",
        .label_fr = "Eublepharis macularius (Gecko léopard)",
        .label_en = "Eublepharis macularius (Leopard gecko)",
        .label_es = "Eublepharis macularius (Geco leopardo)",
        .habitat = "Zones rocheuses semi-désertiques",
        .schedule = {
            .day_start_minute = 8 * 60,
            .night_start_minute = 22 * 60,
            .day = { .temp_c = 32.0f, .humidity_pct = 45.0f, .temp_hysteresis_c = 1.5f, .humidity_hysteresis_pct = 8.0f },
            .night = { .temp_c = 24.0f, .humidity_pct = 60.0f, .temp_hysteresis_c = 2.0f, .humidity_hysteresis_pct = 10.0f },
            .day_uvi_max = 2.5f,
            .night_uvi_max = 0.0f,
        },
    },
};

static nvs_handle_t s_nvs = 0;
static bool s_loaded = false;
static custom_entry_t s_custom[SPECIES_CUSTOM_MAX];
static char s_active_key[32] = {0};

static esp_err_t persist_custom(void)
{
    if (!s_nvs) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_blob(s_nvs, SPECIES_KEY_CUSTOM, s_custom, sizeof(s_custom));
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist custom profiles: %s", esp_err_to_name(err));
    }
    return err;
}

static void slugify(const char *name, char *out, size_t out_len)
{
    size_t w = 0;
    for (size_t i = 0; name && name[i] != '\0' && w + 1 < out_len; ++i) {
        char c = name[i];
        if (isalnum((unsigned char)c)) {
            out[w++] = (char)tolower((unsigned char)c);
        } else if (c == ' ' || c == '-' || c == '_' || c == '/') {
            if (w > 0 && out[w - 1] != '_') {
                out[w++] = '_';
            }
        }
    }
    if (w == 0) {
        strlcpy(out, "profile", out_len);
    } else {
        out[w] = '\0';
    }
}

static const species_profile_t *find_builtin(const char *key)
{
    if (!key) {
        return NULL;
    }
    for (size_t i = 0; i < species_profiles_builtin_count(); ++i) {
        if (strcmp(s_builtin[i].key, key) == 0) {
            return &s_builtin[i];
        }
    }
    return NULL;
}

static custom_entry_t *find_custom(const char *key)
{
    if (!key) {
        return NULL;
    }
    for (size_t i = 0; i < SPECIES_CUSTOM_MAX; ++i) {
        if (s_custom[i].used && strcmp(s_custom[i].key, key) == 0) {
            return &s_custom[i];
        }
    }
    return NULL;
}

static esp_err_t load_state(void)
{
    if (s_loaded) {
        return ESP_OK;
    }
    esp_err_t err = nvs_open(SPECIES_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", SPECIES_NAMESPACE, esp_err_to_name(err));
        return err;
    }
    size_t required = sizeof(s_custom);
    err = nvs_get_blob(s_nvs, SPECIES_KEY_CUSTOM, s_custom, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(s_custom, 0, sizeof(s_custom));
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load custom profiles: %s", esp_err_to_name(err));
        memset(s_custom, 0, sizeof(s_custom));
        err = ESP_OK;
    }
    size_t key_len = sizeof(s_active_key);
    esp_err_t key_err = nvs_get_str(s_nvs, SPECIES_KEY_ACTIVE, s_active_key, &key_len);
    if (key_err != ESP_OK) {
        s_active_key[0] = '\0';
    }
    s_loaded = true;
    return err;
}

esp_err_t species_profiles_init(void)
{
    ESP_RETURN_ON_ERROR(load_state(), TAG, "Failed to init species profiles");
    if (s_active_key[0] == '\0') {
        strlcpy(s_active_key, s_builtin[0].key, sizeof(s_active_key));
        ESP_LOGI(TAG, "Defaulting active profile to %s", s_active_key);
        species_profiles_apply(s_active_key);
    }
    return ESP_OK;
}

size_t species_profiles_builtin_count(void)
{
    return sizeof(s_builtin) / sizeof(s_builtin[0]);
}

const species_profile_t *species_profiles_builtin(size_t index)
{
    if (index >= species_profiles_builtin_count()) {
        return NULL;
    }
    return &s_builtin[index];
}

size_t species_profiles_custom_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < SPECIES_CUSTOM_MAX; ++i) {
        if (s_custom[i].used) {
            ++count;
        }
    }
    return count;
}

esp_err_t species_profiles_custom_get(size_t index, species_custom_profile_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t seen = 0;
    for (size_t i = 0; i < SPECIES_CUSTOM_MAX; ++i) {
        if (!s_custom[i].used) {
            continue;
        }
        if (seen == index) {
            memset(out, 0, sizeof(*out));
            strlcpy(out->key, s_custom[i].key, sizeof(out->key));
            strlcpy(out->name, s_custom[i].name, sizeof(out->name));
            out->schedule = s_custom[i].schedule;
            return ESP_OK;
        }
        ++seen;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t species_profiles_apply(const char *key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state not loaded");
    const species_profile_t *builtin = find_builtin(key);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (builtin) {
        err = climate_update_targets(&builtin->schedule);
    } else {
        custom_entry_t *entry = find_custom(key);
        if (!entry) {
            return ESP_ERR_NOT_FOUND;
        }
        err = climate_update_targets(&entry->schedule);
    }
    if (err == ESP_OK) {
        strlcpy(s_active_key, key, sizeof(s_active_key));
        if (s_nvs) {
            esp_err_t set_err = nvs_set_str(s_nvs, SPECIES_KEY_ACTIVE, s_active_key);
            if (set_err == ESP_OK) {
                set_err = nvs_commit(s_nvs);
            }
            if (set_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to persist active profile: %s", esp_err_to_name(set_err));
            }
        }
    }
    return err;
}

esp_err_t species_profiles_get_active_key(char *out, size_t len)
{
    if (!out || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state not loaded");
    if (s_active_key[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    strlcpy(out, s_active_key, len);
    return ESP_OK;
}

esp_err_t species_profiles_save_custom(const char *name, const climate_schedule_t *schedule, char *out_key, size_t out_len)
{
    if (!name || !schedule) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state not loaded");
    char slug[24] = {0};
    slugify(name, slug, sizeof(slug));
    char key[32];
    snprintf(key, sizeof(key), "custom:%s", slug);

    custom_entry_t *slot = find_custom(key);
    if (!slot) {
        for (size_t i = 0; i < SPECIES_CUSTOM_MAX; ++i) {
            if (!s_custom[i].used) {
                slot = &s_custom[i];
                break;
            }
        }
    }
    if (!slot) {
        return ESP_ERR_NO_MEM;
    }
    slot->used = 1;
    strlcpy(slot->key, key, sizeof(slot->key));
    strlcpy(slot->name, name, sizeof(slot->name));
    slot->schedule = *schedule;

    esp_err_t err = persist_custom();
    if (err == ESP_OK && out_key && out_len > 0) {
        strlcpy(out_key, slot->key, out_len);
    }
    return err;
}

esp_err_t species_profiles_delete_custom(const char *key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state not loaded");
    custom_entry_t *entry = find_custom(key);
    if (!entry) {
        return ESP_ERR_NOT_FOUND;
    }
    entry->used = 0;
    entry->key[0] = '\0';
    entry->name[0] = '\0';
    memset(&entry->schedule, 0, sizeof(entry->schedule));
    return persist_custom();
}
