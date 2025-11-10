#include "species_profiles.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

#include "species_builtin_tlv.h"

#define TAG "SPECIES"

#define SPECIES_NAMESPACE       "species"
#define SPECIES_KEY_ACTIVE      "active_key"
#define SPECIES_KEY_CUSTOM_V1   "custom_blob_v1"
#define SPECIES_KEY_CUSTOM_V2   "custom_tlv_v2"
#define SPECIES_KEY_SECRET      "import_secret"

#define CUSTOM_BLOB_VERSION     2u

#define CUSTOM_TLV_VERSION          0x80
#define CUSTOM_TLV_SCHEDULE_POOL    0x81
#define CUSTOM_TLV_SCHEDULE_ENTRY   0x82
#define CUSTOM_TLV_SCHEDULE_ID      0x83
#define CUSTOM_TLV_SCHEDULE_CRC32   0x84
#define CUSTOM_TLV_PROFILES         0x85
#define CUSTOM_TLV_PROFILE_ENTRY    0x86
#define CUSTOM_TLV_PROFILE_NAME     0x87
#define CUSTOM_TLV_SCHEDULE_REF     0x88

typedef struct {
    uint8_t type;
    const uint8_t *value;
    uint16_t length;
} tlv_entry_t;

typedef struct {
    climate_schedule_t schedule;
    uint32_t crc32;
    uint16_t refcount;
} schedule_pool_entry_t;

typedef struct {
    char key[32];
    char name[64];
    uint16_t schedule_index;
    char *habitat;
    char *uv_index_category;
    char *season_cycle;
    float uv_index_peak;
} custom_profile_internal_t;

typedef struct {
    schedule_pool_entry_t *schedules;
    size_t schedule_count;
    size_t schedule_capacity;
    custom_profile_internal_t *profiles;
    size_t profile_count;
    size_t profile_capacity;
} custom_storage_t;

static nvs_handle_t s_nvs = 0;
static bool s_loaded = false;
static bool s_builtin_loaded = false;

static species_profile_t *s_builtin_profiles = NULL;
static size_t s_builtin_count = 0;

static schedule_pool_entry_t *s_schedule_pool = NULL;
static size_t s_schedule_pool_count = 0;
static size_t s_schedule_pool_capacity = 0;

static custom_profile_internal_t *s_custom_profiles = NULL;
static size_t s_custom_count = 0;
static size_t s_custom_capacity = 0;

static char s_active_key[48] = {0};
static uint8_t s_import_secret[32];
static bool s_secret_loaded = false;

static void free_builtin_profiles(void)
{
    if (!s_builtin_profiles) {
        return;
    }
    for (size_t i = 0; i < s_builtin_count; ++i) {
        const species_profile_t *profile = &s_builtin_profiles[i];
        free((char *)profile->key);
        if (profile->labels) {
            for (size_t l = 0; l < profile->label_count; ++l) {
                free((char *)profile->labels[l].label);
            }
            free((species_label_entry_t *)profile->labels);
        }
        free((char *)profile->metadata.habitat);
        free((char *)profile->metadata.uv_index_category);
        free((char *)profile->metadata.season_cycle);
    }
    free(s_builtin_profiles);
    s_builtin_profiles = NULL;
    s_builtin_count = 0;
    s_builtin_loaded = false;
}

static bool tlv_next(const uint8_t *buffer, size_t len, size_t *offset, tlv_entry_t *out)
{
    if (!buffer || !offset || !out) {
        return false;
    }
    if (*offset + 3 > len) {
        return false;
    }
    out->type = buffer[*offset];
    uint16_t value_len = (uint16_t)(((uint16_t)buffer[*offset + 1] << 8) | buffer[*offset + 2]);
    *offset += 3;
    if (*offset + value_len > len) {
        return false;
    }
    out->value = buffer + *offset;
    out->length = value_len;
    *offset += value_len;
    return true;
}

static char *dup_bytes(const uint8_t *data, size_t len)
{
    if (!data && len > 0) {
        return NULL;
    }
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    if (len > 0 && data) {
        memcpy(out, data, len);
    }
    out[len] = '\0';
    return out;
}

static char *dup_string(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, src, len + 1);
    return out;
}

static void slugify(const char *name, char *out, size_t out_len)
{
    size_t w = 0;
    if (!out || out_len == 0) {
        return;
    }
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

static uint32_t schedule_crc32(const climate_schedule_t *schedule)
{
    return esp_rom_crc32_le(0, (const uint8_t *)schedule, sizeof(*schedule));
}

static void custom_profile_free(custom_profile_internal_t *profile)
{
    if (!profile) {
        return;
    }
    free(profile->habitat);
    profile->habitat = NULL;
    free(profile->uv_index_category);
    profile->uv_index_category = NULL;
    free(profile->season_cycle);
    profile->season_cycle = NULL;
}

static void free_custom_profiles(void)
{
    if (!s_custom_profiles) {
        return;
    }
    for (size_t i = 0; i < s_custom_count; ++i) {
        custom_profile_free(&s_custom_profiles[i]);
    }
    free(s_custom_profiles);
    s_custom_profiles = NULL;
    s_custom_count = 0;
    s_custom_capacity = 0;
}

static void free_schedule_pool(void)
{
    free(s_schedule_pool);
    s_schedule_pool = NULL;
    s_schedule_pool_count = 0;
    s_schedule_pool_capacity = 0;
}

static esp_err_t schedule_pool_reserve(size_t capacity)
{
    if (capacity <= s_schedule_pool_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_schedule_pool_capacity ? s_schedule_pool_capacity : 4;
    while (new_capacity < capacity) {
        new_capacity *= 2;
    }
    schedule_pool_entry_t *tmp = (schedule_pool_entry_t *)realloc(s_schedule_pool, new_capacity * sizeof(schedule_pool_entry_t));
    if (!tmp) {
        return ESP_ERR_NO_MEM;
    }
    s_schedule_pool = tmp;
    s_schedule_pool_capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t custom_profiles_reserve(size_t capacity)
{
    if (capacity <= s_custom_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_custom_capacity ? s_custom_capacity : 4;
    while (new_capacity < capacity) {
        new_capacity *= 2;
    }
    custom_profile_internal_t *tmp = (custom_profile_internal_t *)realloc(s_custom_profiles, new_capacity * sizeof(custom_profile_internal_t));
    if (!tmp) {
        return ESP_ERR_NO_MEM;
    }
    s_custom_profiles = tmp;
    s_custom_capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t schedule_pool_attach(const climate_schedule_t *schedule, uint16_t *out_index)
{
    if (!schedule || !out_index) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t crc = schedule_crc32(schedule);
    for (size_t i = 0; i < s_schedule_pool_count; ++i) {
        if (s_schedule_pool[i].crc32 == crc && memcmp(&s_schedule_pool[i].schedule, schedule, sizeof(*schedule)) == 0) {
            if (s_schedule_pool[i].refcount < UINT16_MAX) {
                s_schedule_pool[i].refcount++;
            }
            *out_index = (uint16_t)i;
            return ESP_OK;
        }
    }
    ESP_RETURN_ON_ERROR(schedule_pool_reserve(s_schedule_pool_count + 1), TAG, "schedule pool grow failed");
    schedule_pool_entry_t *entry = &s_schedule_pool[s_schedule_pool_count];
    entry->schedule = *schedule;
    entry->crc32 = crc;
    entry->refcount = 1;
    *out_index = (uint16_t)s_schedule_pool_count;
    s_schedule_pool_count++;
    return ESP_OK;
}

static void schedule_pool_release(uint16_t index)
{
    if (index >= s_schedule_pool_count) {
        return;
    }
    schedule_pool_entry_t *entry = &s_schedule_pool[index];
    if (entry->refcount > 0) {
        entry->refcount--;
    }
    if (entry->refcount == 0) {
        size_t last = s_schedule_pool_count - 1;
        if (index != last) {
            schedule_pool_entry_t moved = s_schedule_pool[last];
            s_schedule_pool[index] = moved;
            for (size_t i = 0; i < s_custom_count; ++i) {
                if (s_custom_profiles[i].schedule_index == last) {
                    s_custom_profiles[i].schedule_index = index;
                }
            }
        }
        s_schedule_pool_count--;
    }
}

static esp_err_t ensure_secret(void)
{
    if (s_secret_loaded) {
        return ESP_OK;
    }
    if (!s_nvs) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t required = sizeof(s_import_secret);
    esp_err_t err = nvs_get_blob(s_nvs, SPECIES_KEY_SECRET, s_import_secret, &required);
    if (err == ESP_OK && required == sizeof(s_import_secret)) {
        s_secret_loaded = true;
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) {
        return err;
    }
    uint8_t secret[sizeof(s_import_secret)];
    esp_fill_random(secret, sizeof(secret));
    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs, SPECIES_KEY_SECRET, secret, sizeof(secret)), TAG, "secret store failed");
    ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG, "secret commit failed");
    memcpy(s_import_secret, secret, sizeof(secret));
    mbedtls_platform_zeroize(secret, sizeof(secret));
    s_secret_loaded = true;
    return ESP_OK;
}

static esp_err_t compute_hmac(const uint8_t *nonce, size_t nonce_len, const uint8_t *payload, size_t payload_len, uint8_t out[32])
{
    ESP_RETURN_ON_ERROR(ensure_secret(), TAG, "secret unavailable");
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        return ESP_ERR_INVALID_STATE;
    }
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int rc = mbedtls_md_setup(&ctx, info, 1);
    if (rc != 0) {
        mbedtls_md_free(&ctx);
        return ESP_ERR_INVALID_STATE;
    }
    rc = mbedtls_md_hmac_starts(&ctx, s_import_secret, sizeof(s_import_secret));
    if (rc == 0) {
        rc = mbedtls_md_hmac_update(&ctx, nonce, nonce_len);
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_update(&ctx, payload, payload_len);
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_finish(&ctx, out);
    }
    mbedtls_md_free(&ctx);
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static esp_err_t parse_schedule(const uint8_t *buffer, size_t len, climate_schedule_t *out)
{
    if (!buffer || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    climate_schedule_t schedule = {0};
    size_t offset = 0;
    while (offset < len) {
        tlv_entry_t entry;
        if (!tlv_next(buffer, len, &offset, &entry)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (entry.type == SPECIES_TLV_DAY_START && entry.length == 2) {
            schedule.day_start_minute = (int)((entry.value[0] << 8) | entry.value[1]);
        } else if (entry.type == SPECIES_TLV_NIGHT_START && entry.length == 2) {
            schedule.night_start_minute = (int)((entry.value[0] << 8) | entry.value[1]);
        } else if (entry.type == SPECIES_TLV_DAY_TEMP && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.day.temp_c, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_DAY_HUMIDITY && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.day.humidity_pct, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_DAY_TEMP_HYST && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.day.temp_hysteresis_c, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_DAY_HUMID_HYST && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.day.humidity_hysteresis_pct, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_DAY_UVI_MAX && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.day_uvi_max, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_NIGHT_TEMP && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.night.temp_c, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_NIGHT_HUMIDITY && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.night.humidity_pct, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_NIGHT_TEMP_HYST && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.night.temp_hysteresis_c, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_NIGHT_HUMID_HYST && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.night.humidity_hysteresis_pct, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_NIGHT_UVI_MAX && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(&schedule.night_uvi_max, &raw, sizeof(raw));
        }
    }
    *out = schedule;
    return ESP_OK;
}

static esp_err_t parse_labels(const uint8_t *buffer, size_t len, species_label_entry_t **out_labels, size_t *out_count)
{
    if (!buffer || !out_labels || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    species_label_entry_t *labels = NULL;
    size_t count = 0;
    size_t offset = 0;
    while (offset < len) {
        tlv_entry_t entry;
        if (!tlv_next(buffer, len, &offset, &entry)) {
            for (size_t i = 0; i < count; ++i) {
                free((char *)labels[i].label);
            }
            free(labels);
            return ESP_ERR_INVALID_STATE;
        }
        if (entry.type != SPECIES_TLV_LABEL_ENTRY) {
            continue;
        }
        char language[6] = {0};
        char *text = NULL;
        size_t inner_off = 0;
        while (inner_off < entry.length) {
            tlv_entry_t inner;
            if (!tlv_next(entry.value, entry.length, &inner_off, &inner)) {
                break;
            }
            if (inner.type == SPECIES_TLV_LABEL_LANG) {
                size_t l = inner.length < sizeof(language) - 1 ? inner.length : sizeof(language) - 1;
                memcpy(language, inner.value, l);
                language[l] = '\0';
            } else if (inner.type == SPECIES_TLV_LABEL_TEXT) {
                text = dup_bytes(inner.value, inner.length);
                if (!text) {
                    break;
                }
            }
        }
        if (language[0] == '\0' || !text) {
            free(text);
            for (size_t i = 0; i < count; ++i) {
                free((char *)labels[i].label);
            }
            free(labels);
            return ESP_ERR_INVALID_STATE;
        }
        species_label_entry_t *tmp = (species_label_entry_t *)realloc(labels, (count + 1) * sizeof(species_label_entry_t));
        if (!tmp) {
            free(text);
            for (size_t i = 0; i < count; ++i) {
                free((char *)labels[i].label);
            }
            free(labels);
            return ESP_ERR_NO_MEM;
        }
        labels = tmp;
        strlcpy(labels[count].code, language, sizeof(labels[count].code));
        labels[count].label = text;
        ++count;
    }
    *out_labels = labels;
    *out_count = count;
    return ESP_OK;
}

static esp_err_t parse_metadata_block(const uint8_t *buffer, size_t len, species_profile_metadata_t *out_meta, char **habitat_out, char **category_out, char **season_out, float *uv_peak)
{
    if (!buffer || !out_meta || !habitat_out || !category_out || !season_out || !uv_peak) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t offset = 0;
    while (offset < len) {
        tlv_entry_t entry;
        if (!tlv_next(buffer, len, &offset, &entry)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (entry.type == SPECIES_TLV_HABITAT) {
            free(*habitat_out);
            *habitat_out = dup_bytes(entry.value, entry.length);
            if (!*habitat_out) {
                return ESP_ERR_NO_MEM;
            }
        } else if (entry.type == SPECIES_TLV_METADATA_UV_PEAK && entry.length == 4) {
            uint32_t raw = (entry.value[0] << 24) | (entry.value[1] << 16) | (entry.value[2] << 8) | entry.value[3];
            memcpy(uv_peak, &raw, sizeof(raw));
        } else if (entry.type == SPECIES_TLV_METADATA_UV_CATEGORY) {
            free(*category_out);
            *category_out = dup_bytes(entry.value, entry.length);
            if (!*category_out) {
                return ESP_ERR_NO_MEM;
            }
        } else if (entry.type == SPECIES_TLV_METADATA_SEASON) {
            free(*season_out);
            *season_out = dup_bytes(entry.value, entry.length);
            if (!*season_out) {
                return ESP_ERR_NO_MEM;
            }
        }
    }
    out_meta->habitat = *habitat_out;
    out_meta->uv_index_category = *category_out;
    out_meta->season_cycle = *season_out;
    out_meta->uv_index_peak = *uv_peak;
    return ESP_OK;
}

static esp_err_t parse_profile(const uint8_t *buffer, size_t len, species_profile_t *out_profile)
{
    if (!buffer || !out_profile) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *key = NULL;
    char *habitat = NULL;
    char *category = NULL;
    char *season = NULL;
    float uv_peak = 0.0f;
    species_label_entry_t *labels = NULL;
    size_t label_count = 0;
    climate_schedule_t schedule = {0};
    size_t offset = 0;
    bool schedule_parsed = false;
    while (offset < len) {
        tlv_entry_t entry;
        if (!tlv_next(buffer, len, &offset, &entry)) {
            goto fail;
        }
        if (entry.type == SPECIES_TLV_KEY) {
            char *dup = dup_bytes(entry.value, entry.length);
            if (!dup) {
                goto fail;
            }
            key = dup;
        } else if (entry.type == SPECIES_TLV_LABELS) {
            if (parse_labels(entry.value, entry.length, &labels, &label_count) != ESP_OK) {
                goto fail;
            }
        } else if (entry.type == SPECIES_TLV_METADATA) {
            if (parse_metadata_block(entry.value, entry.length, &(species_profile_metadata_t){0}, &habitat, &category, &season, &uv_peak) != ESP_OK) {
                goto fail;
            }
        } else if (entry.type == SPECIES_TLV_HABITAT) {
            free(habitat);
            habitat = dup_bytes(entry.value, entry.length);
            if (!habitat) {
                goto fail;
            }
        } else if (entry.type == SPECIES_TLV_SCHEDULE) {
            if (parse_schedule(entry.value, entry.length, &schedule) != ESP_OK) {
                goto fail;
            }
            schedule_parsed = true;
        }
    }
    if (!key || label_count == 0 || !schedule_parsed) {
        goto fail;
    }
    out_profile->key = key;
    out_profile->labels = labels;
    out_profile->label_count = label_count;
    out_profile->metadata.habitat = habitat;
    out_profile->metadata.uv_index_category = category;
    out_profile->metadata.season_cycle = season;
    out_profile->metadata.uv_index_peak = uv_peak;
    out_profile->schedule = schedule;
    return ESP_OK;
fail:
    free((char *)key);
    for (size_t i = 0; i < label_count; ++i) {
        free((char *)labels[i].label);
    }
    free(labels);
    free(habitat);
    free(category);
    free(season);
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t load_builtin_profiles(void)
{
    if (s_builtin_loaded) {
        return ESP_OK;
    }
    size_t offset = 0;
    while (offset < species_builtin_tlv_len) {
        tlv_entry_t entry;
        if (!tlv_next(species_builtin_tlv, species_builtin_tlv_len, &offset, &entry)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (entry.type != SPECIES_TLV_PROFILE) {
            continue;
        }
        species_profile_t profile = {0};
        esp_err_t err = parse_profile(entry.value, entry.length, &profile);
        if (err != ESP_OK) {
            return err;
        }
        species_profile_t *tmp = (species_profile_t *)realloc(s_builtin_profiles, (s_builtin_count + 1) * sizeof(species_profile_t));
        if (!tmp) {
            free((char *)profile.key);
            for (size_t i = 0; i < profile.label_count; ++i) {
                free((char *)profile.labels[i].label);
            }
            free((species_label_entry_t *)profile.labels);
            free((char *)profile.metadata.habitat);
            free((char *)profile.metadata.uv_index_category);
            free((char *)profile.metadata.season_cycle);
            return ESP_ERR_NO_MEM;
        }
        s_builtin_profiles = tmp;
        s_builtin_profiles[s_builtin_count++] = profile;
    }
    s_builtin_loaded = true;
    return ESP_OK;
}

static const species_profile_t *find_builtin(const char *key)
{
    if (!key) {
        return NULL;
    }
    for (size_t i = 0; i < s_builtin_count; ++i) {
        if (strcmp(s_builtin_profiles[i].key, key) == 0) {
            return &s_builtin_profiles[i];
        }
    }
    return NULL;
}

static custom_profile_internal_t *find_custom(const char *key, size_t *out_index)
{
    if (!key) {
        return NULL;
    }
    for (size_t i = 0; i < s_custom_count; ++i) {
        if (strcmp(s_custom_profiles[i].key, key) == 0) {
            if (out_index) {
                *out_index = i;
            }
            return &s_custom_profiles[i];
        }
    }
    return NULL;
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t capacity;
} tlv_buffer_t;

static void tlv_buffer_free(tlv_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

static esp_err_t tlv_buffer_reserve(tlv_buffer_t *buf, size_t additional)
{
    if (!buf) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t required = buf->len + additional;
    if (required <= buf->capacity) {
        return ESP_OK;
    }
    size_t new_capacity = buf->capacity ? buf->capacity : 64;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    uint8_t *tmp = (uint8_t *)realloc(buf->data, new_capacity);
    if (!tmp) {
        return ESP_ERR_NO_MEM;
    }
    buf->data = tmp;
    buf->capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t tlv_buffer_append(tlv_buffer_t *buf, const void *data, size_t len)
{
    if (!buf || (!data && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(tlv_buffer_reserve(buf, len), TAG, "buffer grow failed");
    if (len > 0 && data) {
        memcpy(buf->data + buf->len, data, len);
    }
    buf->len += len;
    return ESP_OK;
}

static esp_err_t tlv_buffer_append_tlv(tlv_buffer_t *buf, uint8_t type, const uint8_t *payload, size_t payload_len)
{
    uint8_t header[3];
    header[0] = type;
    header[1] = (uint8_t)((payload_len >> 8) & 0xFF);
    header[2] = (uint8_t)(payload_len & 0xFF);
    ESP_RETURN_ON_ERROR(tlv_buffer_append(buf, header, sizeof(header)), TAG, "tlv header append failed");
    return tlv_buffer_append(buf, payload, payload_len);
}

static esp_err_t tlv_buffer_append_u16(tlv_buffer_t *buf, uint8_t type, uint16_t value)
{
    uint8_t payload[2] = { (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF) };
    return tlv_buffer_append_tlv(buf, type, payload, sizeof(payload));
}

static esp_err_t tlv_buffer_append_u32(tlv_buffer_t *buf, uint8_t type, uint32_t value)
{
    uint8_t payload[4] = {
        (uint8_t)((value >> 24) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)(value & 0xFF)
    };
    return tlv_buffer_append_tlv(buf, type, payload, sizeof(payload));
}

static esp_err_t tlv_buffer_append_float(tlv_buffer_t *buf, uint8_t type, float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    return tlv_buffer_append_u32(buf, type, raw);
}

static esp_err_t tlv_buffer_append_string(tlv_buffer_t *buf, uint8_t type, const char *str)
{
    if (!str) {
        return ESP_OK;
    }
    size_t len = strlen(str);
    return tlv_buffer_append_tlv(buf, type, (const uint8_t *)str, len);
}

static esp_err_t encode_schedule_tlv(const climate_schedule_t *schedule, tlv_buffer_t *buf)
{
    tlv_buffer_t inner = {0};
    esp_err_t err = tlv_buffer_append_u16(&inner, SPECIES_TLV_DAY_START, (uint16_t)schedule->day_start_minute);
    if (err == ESP_OK) {
        err = tlv_buffer_append_u16(&inner, SPECIES_TLV_NIGHT_START, (uint16_t)schedule->night_start_minute);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_DAY_TEMP, schedule->day.temp_c);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_DAY_HUMIDITY, schedule->day.humidity_pct);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_DAY_TEMP_HYST, schedule->day.temp_hysteresis_c);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_DAY_HUMID_HYST, schedule->day.humidity_hysteresis_pct);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_DAY_UVI_MAX, schedule->day_uvi_max);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_NIGHT_TEMP, schedule->night.temp_c);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_NIGHT_HUMIDITY, schedule->night.humidity_pct);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_NIGHT_TEMP_HYST, schedule->night.temp_hysteresis_c);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_NIGHT_HUMID_HYST, schedule->night.humidity_hysteresis_pct);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_NIGHT_UVI_MAX, schedule->night_uvi_max);
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_tlv(buf, SPECIES_TLV_SCHEDULE, inner.data, inner.len);
    }
    tlv_buffer_free(&inner);
    return err;
}

static esp_err_t encode_metadata_tlv(const species_profile_metadata_t *meta, tlv_buffer_t *buf)
{
    if (!meta) {
        return ESP_OK;
    }
    tlv_buffer_t inner = {0};
    esp_err_t err = ESP_OK;
    if (meta->habitat) {
        err = tlv_buffer_append_tlv(&inner, SPECIES_TLV_HABITAT, (const uint8_t *)meta->habitat, strlen(meta->habitat));
    }
    if (err == ESP_OK && meta->uv_index_category) {
        err = tlv_buffer_append_tlv(&inner, SPECIES_TLV_METADATA_UV_CATEGORY, (const uint8_t *)meta->uv_index_category, strlen(meta->uv_index_category));
    }
    if (err == ESP_OK && meta->season_cycle) {
        err = tlv_buffer_append_tlv(&inner, SPECIES_TLV_METADATA_SEASON, (const uint8_t *)meta->season_cycle, strlen(meta->season_cycle));
    }
    if (err == ESP_OK) {
        err = tlv_buffer_append_float(&inner, SPECIES_TLV_METADATA_UV_PEAK, meta->uv_index_peak);
    }
    if (err == ESP_OK && inner.len > 0) {
        err = tlv_buffer_append_tlv(buf, SPECIES_TLV_METADATA, inner.data, inner.len);
    }
    tlv_buffer_free(&inner);
    return err;
}
static esp_err_t custom_storage_ensure_schedule_capacity(custom_storage_t *storage, size_t capacity)
{
    if (capacity <= storage->schedule_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = storage->schedule_capacity ? storage->schedule_capacity : 4;
    while (new_capacity < capacity) {
        new_capacity *= 2;
    }
    schedule_pool_entry_t *tmp = (schedule_pool_entry_t *)realloc(storage->schedules, new_capacity * sizeof(schedule_pool_entry_t));
    if (!tmp) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = storage->schedule_capacity; i < new_capacity; ++i) {
        tmp[i] = (schedule_pool_entry_t){0};
    }
    storage->schedules = tmp;
    storage->schedule_capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t custom_storage_add_schedule(custom_storage_t *storage, uint16_t id, const climate_schedule_t *schedule, uint32_t crc)
{
    ESP_RETURN_ON_ERROR(custom_storage_ensure_schedule_capacity(storage, (size_t)id + 1), TAG, "schedule capacity");
    storage->schedules[id].schedule = *schedule;
    storage->schedules[id].crc32 = crc;
    storage->schedules[id].refcount = 0;
    if ((size_t)id + 1 > storage->schedule_count) {
        storage->schedule_count = (size_t)id + 1;
    }
    return ESP_OK;
}

static esp_err_t custom_storage_ensure_profile_capacity(custom_storage_t *storage, size_t capacity)
{
    if (capacity <= storage->profile_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = storage->profile_capacity ? storage->profile_capacity : 4;
    while (new_capacity < capacity) {
        new_capacity *= 2;
    }
    custom_profile_internal_t *tmp = (custom_profile_internal_t *)realloc(storage->profiles, new_capacity * sizeof(custom_profile_internal_t));
    if (!tmp) {
        return ESP_ERR_NO_MEM;
    }
    storage->profiles = tmp;
    storage->profile_capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t custom_storage_add_profile(custom_storage_t *storage, const custom_profile_internal_t *profile)
{
    ESP_RETURN_ON_ERROR(custom_storage_ensure_profile_capacity(storage, storage->profile_count + 1), TAG, "profile capacity");
    storage->profiles[storage->profile_count++] = *profile;
    return ESP_OK;
}

static void custom_storage_free(custom_storage_t *storage)
{
    if (!storage) {
        return;
    }
    if (storage->profiles) {
        for (size_t i = 0; i < storage->profile_count; ++i) {
            custom_profile_free(&storage->profiles[i]);
        }
    }
    free(storage->profiles);
    free(storage->schedules);
    storage->profiles = NULL;
    storage->schedules = NULL;
    storage->profile_count = storage->profile_capacity = 0;
    storage->schedule_count = storage->schedule_capacity = 0;
}

static esp_err_t parse_schedule_pool_block(const uint8_t *buffer, size_t len, custom_storage_t *storage)
{
    size_t offset = 0;
    while (offset < len) {
        tlv_entry_t entry;
        if (!tlv_next(buffer, len, &offset, &entry)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (entry.type != CUSTOM_TLV_SCHEDULE_ENTRY) {
            continue;
        }
        uint16_t id = 0;
        uint32_t crc = 0;
        bool have_id = false;
        bool have_schedule = false;
        climate_schedule_t schedule = {0};
        size_t inner_off = 0;
        while (inner_off < entry.length) {
            tlv_entry_t inner;
            if (!tlv_next(entry.value, entry.length, &inner_off, &inner)) {
                return ESP_ERR_INVALID_STATE;
            }
            if (inner.type == CUSTOM_TLV_SCHEDULE_ID && inner.length == 2) {
                id = (uint16_t)((inner.value[0] << 8) | inner.value[1]);
                have_id = true;
            } else if (inner.type == CUSTOM_TLV_SCHEDULE_CRC32 && inner.length == 4) {
                crc = ((uint32_t)inner.value[0] << 24) | ((uint32_t)inner.value[1] << 16) |
                      ((uint32_t)inner.value[2] << 8) | (uint32_t)inner.value[3];
            } else if (inner.type == SPECIES_TLV_SCHEDULE) {
                ESP_RETURN_ON_ERROR(parse_schedule(inner.value, inner.length, &schedule), TAG, "schedule parse");
                have_schedule = true;
            }
        }
        if (!have_id || !have_schedule) {
            return ESP_ERR_INVALID_STATE;
        }
        ESP_RETURN_ON_ERROR(custom_storage_add_schedule(storage, id, &schedule, crc), TAG, "schedule add");
    }
    return ESP_OK;
}

static esp_err_t parse_profiles_block(const uint8_t *buffer, size_t len, custom_storage_t *storage)
{
    size_t offset = 0;
    while (offset < len) {
        tlv_entry_t entry;
        if (!tlv_next(buffer, len, &offset, &entry)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (entry.type != CUSTOM_TLV_PROFILE_ENTRY) {
            continue;
        }
        custom_profile_internal_t profile = {0};
        char *habitat = NULL;
        char *category = NULL;
        char *season = NULL;
        float uv_peak = 0.0f;
        bool have_key = false;
        bool have_name = false;
        bool have_schedule_ref = false;
        size_t inner_off = 0;
        while (inner_off < entry.length) {
            tlv_entry_t inner;
            if (!tlv_next(entry.value, entry.length, &inner_off, &inner)) {
                free(habitat);
                free(category);
                free(season);
                return ESP_ERR_INVALID_STATE;
            }
            if (inner.type == SPECIES_TLV_KEY) {
                size_t copy = inner.length < sizeof(profile.key) - 1 ? inner.length : sizeof(profile.key) - 1;
                memcpy(profile.key, inner.value, copy);
                profile.key[copy] = '\0';
                have_key = true;
            } else if (inner.type == CUSTOM_TLV_PROFILE_NAME) {
                size_t copy = inner.length < sizeof(profile.name) - 1 ? inner.length : sizeof(profile.name) - 1;
                memcpy(profile.name, inner.value, copy);
                profile.name[copy] = '\0';
                have_name = true;
            } else if (inner.type == CUSTOM_TLV_SCHEDULE_REF && inner.length == 2) {
                profile.schedule_index = (uint16_t)((inner.value[0] << 8) | inner.value[1]);
                have_schedule_ref = true;
            } else if (inner.type == SPECIES_TLV_METADATA) {
                if (parse_metadata_block(inner.value, inner.length, &(species_profile_metadata_t){0}, &habitat, &category, &season, &uv_peak) != ESP_OK) {
                    free(habitat);
                    free(category);
                    free(season);
                    return ESP_ERR_INVALID_STATE;
                }
            } else if (inner.type == SPECIES_TLV_HABITAT) {
                free(habitat);
                habitat = dup_bytes(inner.value, inner.length);
                if (!habitat) {
                    free(category);
                    free(season);
                    return ESP_ERR_NO_MEM;
                }
            }
        }
        if (!have_key || !have_name || !have_schedule_ref) {
            free(habitat);
            free(category);
            free(season);
            return ESP_ERR_INVALID_STATE;
        }
        if (profile.schedule_index >= storage->schedule_count) {
            free(habitat);
            free(category);
            free(season);
            return ESP_ERR_INVALID_STATE;
        }
        profile.habitat = habitat;
        profile.uv_index_category = category;
        profile.season_cycle = season;
        profile.uv_index_peak = uv_peak;
        storage->schedules[profile.schedule_index].refcount++;
        ESP_RETURN_ON_ERROR(custom_storage_add_profile(storage, &profile), TAG, "profile add");
    }
    return ESP_OK;
}

static esp_err_t parse_custom_blob(const uint8_t *blob, size_t len, custom_storage_t *storage)
{
    size_t offset = 0;
    uint8_t version = 0;
    while (offset < len) {
        tlv_entry_t entry;
        if (!tlv_next(blob, len, &offset, &entry)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (entry.type == CUSTOM_TLV_VERSION && entry.length == 1) {
            version = entry.value[0];
        } else if (entry.type == CUSTOM_TLV_SCHEDULE_POOL) {
            ESP_RETURN_ON_ERROR(parse_schedule_pool_block(entry.value, entry.length, storage), TAG, "schedule pool");
        } else if (entry.type == CUSTOM_TLV_PROFILES) {
            ESP_RETURN_ON_ERROR(parse_profiles_block(entry.value, entry.length, storage), TAG, "profiles block");
        }
    }
    if (version != CUSTOM_BLOB_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    return ESP_OK;
}

static esp_err_t build_custom_blob(uint8_t **out, size_t *out_len)
{
    if (!out || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    tlv_buffer_t root = {0};
    uint8_t version = CUSTOM_BLOB_VERSION;
    ESP_RETURN_ON_ERROR(tlv_buffer_append_tlv(&root, CUSTOM_TLV_VERSION, &version, sizeof(version)), TAG, "version tlv");

    tlv_buffer_t schedules = {0};
    for (size_t i = 0; i < s_schedule_pool_count; ++i) {
        if (s_schedule_pool[i].refcount == 0) {
            continue;
        }
        tlv_buffer_t entry = {0};
        esp_err_t err = tlv_buffer_append_u16(&entry, CUSTOM_TLV_SCHEDULE_ID, (uint16_t)i);
        if (err == ESP_OK) {
            err = tlv_buffer_append_u32(&entry, CUSTOM_TLV_SCHEDULE_CRC32, s_schedule_pool[i].crc32);
        }
        if (err == ESP_OK) {
            err = encode_schedule_tlv(&s_schedule_pool[i].schedule, &entry);
        }
        if (err != ESP_OK) {
            tlv_buffer_free(&entry);
            tlv_buffer_free(&schedules);
            tlv_buffer_free(&root);
            return err;
        }
        err = tlv_buffer_append_tlv(&schedules, CUSTOM_TLV_SCHEDULE_ENTRY, entry.data, entry.len);
        tlv_buffer_free(&entry);
        if (err != ESP_OK) {
            tlv_buffer_free(&schedules);
            tlv_buffer_free(&root);
            return err;
        }
    }
    if (schedules.len > 0) {
        esp_err_t err = tlv_buffer_append_tlv(&root, CUSTOM_TLV_SCHEDULE_POOL, schedules.data, schedules.len);
        if (err != ESP_OK) {
            tlv_buffer_free(&schedules);
            tlv_buffer_free(&root);
            return err;
        }
    }
    tlv_buffer_free(&schedules);

    tlv_buffer_t profiles = {0};
    for (size_t i = 0; i < s_custom_count; ++i) {
        const custom_profile_internal_t *profile = &s_custom_profiles[i];
        tlv_buffer_t entry = {0};
        esp_err_t err = tlv_buffer_append_tlv(&entry, SPECIES_TLV_KEY, (const uint8_t *)profile->key, strlen(profile->key));
        if (err == ESP_OK) {
            err = tlv_buffer_append_tlv(&entry, CUSTOM_TLV_PROFILE_NAME, (const uint8_t *)profile->name, strlen(profile->name));
        }
        if (err == ESP_OK) {
            err = tlv_buffer_append_u16(&entry, CUSTOM_TLV_SCHEDULE_REF, profile->schedule_index);
        }
        if (err == ESP_OK) {
            species_profile_metadata_t meta = {
                .habitat = profile->habitat,
                .uv_index_category = profile->uv_index_category,
                .season_cycle = profile->season_cycle,
                .uv_index_peak = profile->uv_index_peak,
            };
            err = encode_metadata_tlv(&meta, &entry);
        }
        if (err != ESP_OK) {
            tlv_buffer_free(&entry);
            tlv_buffer_free(&profiles);
            tlv_buffer_free(&root);
            return err;
        }
        err = tlv_buffer_append_tlv(&profiles, CUSTOM_TLV_PROFILE_ENTRY, entry.data, entry.len);
        tlv_buffer_free(&entry);
        if (err != ESP_OK) {
            tlv_buffer_free(&profiles);
            tlv_buffer_free(&root);
            return err;
        }
    }
    if (profiles.len > 0) {
        esp_err_t err = tlv_buffer_append_tlv(&root, CUSTOM_TLV_PROFILES, profiles.data, profiles.len);
        if (err != ESP_OK) {
            tlv_buffer_free(&profiles);
            tlv_buffer_free(&root);
            return err;
        }
    }
    tlv_buffer_free(&profiles);

    *out = root.data;
    *out_len = root.len;
    root.data = NULL;
    root.len = 0;
    root.capacity = 0;
    return ESP_OK;
}

static esp_err_t adopt_custom_storage(custom_storage_t *storage)
{
    if (!storage) {
        return ESP_ERR_INVALID_ARG;
    }
    free_custom_profiles();
    free_schedule_pool();
    s_schedule_pool = storage->schedules;
    s_schedule_pool_count = storage->schedule_count;
    s_schedule_pool_capacity = storage->schedule_capacity;
    s_custom_profiles = storage->profiles;
    s_custom_count = storage->profile_count;
    s_custom_capacity = storage->profile_capacity;
    storage->schedules = NULL;
    storage->profiles = NULL;
    storage->schedule_count = storage->schedule_capacity = 0;
    storage->profile_count = storage->profile_capacity = 0;
    return ESP_OK;
}
static esp_err_t storage_attach_schedule(custom_storage_t *storage, const climate_schedule_t *schedule, uint16_t *out_index)
{
    uint32_t crc = schedule_crc32(schedule);
    for (size_t i = 0; i < storage->schedule_count; ++i) {
        if (storage->schedules[i].crc32 == crc && memcmp(&storage->schedules[i].schedule, schedule, sizeof(*schedule)) == 0) {
            if (storage->schedules[i].refcount < UINT16_MAX) {
                storage->schedules[i].refcount++;
            }
            *out_index = (uint16_t)i;
            return ESP_OK;
        }
    }
    uint16_t id = (uint16_t)storage->schedule_count;
    ESP_RETURN_ON_ERROR(custom_storage_add_schedule(storage, id, schedule, crc), TAG, "storage schedule add");
    storage->schedules[id].refcount = 1;
    *out_index = id;
    return ESP_OK;
}
static esp_err_t persist_custom(void)
{
    if (!s_nvs) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    esp_err_t err = build_custom_blob(&blob, &blob_len);
    if (err != ESP_OK) {
        return err;
    }
    esp_err_t store_err = nvs_set_blob(s_nvs, SPECIES_KEY_CUSTOM_V2, blob, blob_len);
    free(blob);
    if (store_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist custom profiles: %s", esp_err_to_name(store_err));
        return store_err;
    }
    store_err = nvs_commit(s_nvs);
    if (store_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit custom profiles: %s", esp_err_to_name(store_err));
    }
    return store_err;
}

typedef struct __attribute__((packed)) {
    uint8_t used;
    char key[32];
    char name[64];
    climate_schedule_t schedule;
} legacy_custom_entry_t;

static esp_err_t migrate_legacy_custom(void)
{
    size_t required = 0;
    esp_err_t err = nvs_get_blob(s_nvs, SPECIES_KEY_CUSTOM_V1, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "legacy size");
    if (required == 0 || required % sizeof(legacy_custom_entry_t) != 0) {
        ESP_LOGW(TAG, "Legacy custom blob has invalid size %zu", required);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t count = required / sizeof(legacy_custom_entry_t);
    legacy_custom_entry_t *entries = (legacy_custom_entry_t *)malloc(required);
    if (!entries) {
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_blob(s_nvs, SPECIES_KEY_CUSTOM_V1, entries, &required);
    if (err != ESP_OK) {
        free(entries);
        return err;
    }
    custom_storage_t storage = {0};
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].used) {
            continue;
        }
        uint16_t sched_index = 0;
        ESP_RETURN_ON_ERROR(storage_attach_schedule(&storage, &entries[i].schedule, &sched_index), TAG, "legacy schedule attach");
        custom_profile_internal_t profile = {0};
        strlcpy(profile.key, entries[i].key, sizeof(profile.key));
        strlcpy(profile.name, entries[i].name, sizeof(profile.name));
        profile.schedule_index = sched_index;
        profile.uv_index_peak = entries[i].schedule.day_uvi_max;
        ESP_RETURN_ON_ERROR(custom_storage_add_profile(&storage, &profile), TAG, "legacy profile add");
    }
    free(entries);
    ESP_RETURN_ON_ERROR(adopt_custom_storage(&storage), TAG, "legacy adopt");
    custom_storage_free(&storage);
    ESP_RETURN_ON_ERROR(nvs_erase_key(s_nvs, SPECIES_KEY_CUSTOM_V1), TAG, "legacy erase");
    ESP_RETURN_ON_ERROR(persist_custom(), TAG, "persist migrated");
    return ESP_OK;
}

static esp_err_t load_custom_from_nvs(void)
{
    size_t required = 0;
    esp_err_t err = nvs_get_blob(s_nvs, SPECIES_KEY_CUSTOM_V2, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return migrate_legacy_custom();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "custom blob size");
    if (required == 0) {
        return ESP_OK;
    }
    uint8_t *blob = (uint8_t *)malloc(required);
    if (!blob) {
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_blob(s_nvs, SPECIES_KEY_CUSTOM_V2, blob, &required);
    if (err != ESP_OK) {
        free(blob);
        return err;
    }
    custom_storage_t storage = {0};
    err = parse_custom_blob(blob, required, &storage);
    free(blob);
    if (err != ESP_OK) {
        custom_storage_free(&storage);
        return err;
    }
    ESP_RETURN_ON_ERROR(adopt_custom_storage(&storage), TAG, "adopt custom");
    custom_storage_free(&storage);
    return ESP_OK;
}

static esp_err_t load_state(void)
{
    if (s_loaded) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(load_builtin_profiles(), TAG, "builtin load");
    esp_err_t err = nvs_open(SPECIES_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", SPECIES_NAMESPACE, esp_err_to_name(err));
        return err;
    }
    err = load_custom_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load custom TLV: %s", esp_err_to_name(err));
        free_custom_profiles();
        free_schedule_pool();
    }
    size_t key_len = sizeof(s_active_key);
    err = nvs_get_str(s_nvs, SPECIES_KEY_ACTIVE, s_active_key, &key_len);
    if (err != ESP_OK) {
        s_active_key[0] = '\0';
    }
    s_loaded = true;
    return ESP_OK;
}
esp_err_t species_profiles_init(void)
{
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    if (s_active_key[0] == '\0' && s_builtin_count > 0) {
        strlcpy(s_active_key, s_builtin_profiles[0].key, sizeof(s_active_key));
        ESP_LOGI(TAG, "Defaulting active profile to %s", s_active_key);
        return species_profiles_apply(s_active_key);
    }
    return ESP_OK;
}

size_t species_profiles_builtin_count(void)
{
    if (!s_builtin_loaded) {
        if (load_builtin_profiles() != ESP_OK) {
            return 0;
        }
    }
    return s_builtin_count;
}

const species_profile_t *species_profiles_builtin(size_t index)
{
    if (!s_builtin_loaded) {
        if (load_builtin_profiles() != ESP_OK) {
            return NULL;
        }
    }
    if (index >= s_builtin_count) {
        return NULL;
    }
    return &s_builtin_profiles[index];
}

size_t species_profiles_custom_count(void)
{
    if (!s_loaded) {
        if (load_state() != ESP_OK) {
            return 0;
        }
    }
    return s_custom_count;
}

esp_err_t species_profiles_custom_get(size_t index, species_custom_profile_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    if (index >= s_custom_count) {
        return ESP_ERR_NOT_FOUND;
    }
    const custom_profile_internal_t *profile = &s_custom_profiles[index];
    if (profile->schedule_index >= s_schedule_pool_count) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));
    strlcpy(out->key, profile->key, sizeof(out->key));
    strlcpy(out->name, profile->name, sizeof(out->name));
    out->schedule = s_schedule_pool[profile->schedule_index].schedule;
    out->uv_index_peak = profile->uv_index_peak;
    if (profile->habitat) {
        strlcpy(out->habitat, profile->habitat, sizeof(out->habitat));
    }
    if (profile->uv_index_category) {
        strlcpy(out->uv_index_category, profile->uv_index_category, sizeof(out->uv_index_category));
    }
    if (profile->season_cycle) {
        strlcpy(out->season_cycle, profile->season_cycle, sizeof(out->season_cycle));
    }
    return ESP_OK;
}

static esp_err_t apply_schedule(const climate_schedule_t *schedule, const char *key)
{
    esp_err_t err = climate_update_targets(schedule);
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

esp_err_t species_profiles_apply(const char *key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    const species_profile_t *builtin = find_builtin(key);
    if (builtin) {
        return apply_schedule(&builtin->schedule, key);
    }
    size_t index = 0;
    custom_profile_internal_t *custom = find_custom(key, &index);
    if (!custom) {
        return ESP_ERR_NOT_FOUND;
    }
    if (custom->schedule_index >= s_schedule_pool_count) {
        return ESP_ERR_INVALID_STATE;
    }
    return apply_schedule(&s_schedule_pool[custom->schedule_index].schedule, key);
}

esp_err_t species_profiles_get_active_key(char *out, size_t len)
{
    if (!out || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    if (s_active_key[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    strlcpy(out, s_active_key, len);
    return ESP_OK;
}

const char *species_profiles_label_for_locale(const species_profile_t *profile, const char *lang, const char *fallback_lang)
{
    if (!profile || !profile->labels || profile->label_count == 0) {
        return NULL;
    }
    if (lang) {
        for (size_t i = 0; i < profile->label_count; ++i) {
            if (strcasecmp(profile->labels[i].code, lang) == 0) {
                return profile->labels[i].label;
            }
        }
    }
    if (fallback_lang) {
        for (size_t i = 0; i < profile->label_count; ++i) {
            if (strcasecmp(profile->labels[i].code, fallback_lang) == 0) {
                return profile->labels[i].label;
            }
        }
    }
    return profile->labels[0].label;
}

static void update_custom_metadata(custom_profile_internal_t *profile, const species_profile_metadata_t *metadata)
{
    if (!profile) {
        return;
    }
    free(profile->habitat);
    free(profile->uv_index_category);
    free(profile->season_cycle);
    profile->habitat = metadata && metadata->habitat ? dup_string(metadata->habitat) : NULL;
    profile->uv_index_category = metadata && metadata->uv_index_category ? dup_string(metadata->uv_index_category) : NULL;
    profile->season_cycle = metadata && metadata->season_cycle ? dup_string(metadata->season_cycle) : NULL;
    profile->uv_index_peak = metadata ? metadata->uv_index_peak : 0.0f;
}

esp_err_t species_profiles_save_custom(const char *name,
                                       const climate_schedule_t *schedule,
                                       const species_profile_metadata_t *metadata,
                                       char *out_key,
                                       size_t out_len)
{
    if (!name || !schedule) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    char slug[24] = {0};
    slugify(name, slug, sizeof(slug));
    char key[32];
    snprintf(key, sizeof(key), "custom:%s", slug);

    custom_profile_internal_t *entry = find_custom(key, NULL);
    uint16_t new_schedule_index = 0;
    ESP_RETURN_ON_ERROR(schedule_pool_attach(schedule, &new_schedule_index), TAG, "schedule attach");
    bool same_schedule = (entry && entry->schedule_index == new_schedule_index);
    if (!entry) {
        ESP_RETURN_ON_ERROR(custom_profiles_reserve(s_custom_count + 1), TAG, "custom reserve");
        entry = &s_custom_profiles[s_custom_count++];
        memset(entry, 0, sizeof(*entry));
        strlcpy(entry->key, key, sizeof(entry->key));
    } else if (!same_schedule) {
        schedule_pool_release(entry->schedule_index);
    }
    entry->schedule_index = new_schedule_index;
    strlcpy(entry->name, name, sizeof(entry->name));
    if (!metadata) {
        species_profile_metadata_t default_meta = {
            .habitat = NULL,
            .uv_index_category = NULL,
            .season_cycle = NULL,
            .uv_index_peak = schedule->day_uvi_max,
        };
        update_custom_metadata(entry, &default_meta);
    } else {
        update_custom_metadata(entry, metadata);
        if (entry->uv_index_peak == 0.0f) {
            entry->uv_index_peak = schedule->day_uvi_max;
        }
    }
    if (same_schedule) {
        schedule_pool_release(new_schedule_index);
    }
    ESP_RETURN_ON_ERROR(persist_custom(), TAG, "persist custom");
    if (out_key && out_len > 0) {
        strlcpy(out_key, entry->key, out_len);
    }
    return ESP_OK;
}

esp_err_t species_profiles_delete_custom(const char *key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    size_t index = 0;
    custom_profile_internal_t *entry = find_custom(key, &index);
    if (!entry) {
        return ESP_ERR_NOT_FOUND;
    }
    schedule_pool_release(entry->schedule_index);
    if (index != s_custom_count - 1) {
        s_custom_profiles[index] = s_custom_profiles[s_custom_count - 1];
    }
    custom_profile_free(&s_custom_profiles[s_custom_count - 1]);
    memset(&s_custom_profiles[s_custom_count - 1], 0, sizeof(s_custom_profiles[s_custom_count - 1]));
    s_custom_count--;
    if (strcmp(s_active_key, key) == 0) {
        s_active_key[0] = '\0';
    }
    ESP_RETURN_ON_ERROR(persist_custom(), TAG, "persist delete");
    if (s_active_key[0] == '\0' && s_builtin_count > 0) {
        strlcpy(s_active_key, s_builtin_profiles[0].key, sizeof(s_active_key));
    }
    return ESP_OK;
}

static bool constant_time_equals(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

esp_err_t species_profiles_export_secure(uint8_t **payload, size_t *payload_len, uint8_t nonce[16], uint8_t signature[32])
{
    if (!payload || !payload_len || !nonce || !signature) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    ESP_RETURN_ON_ERROR(build_custom_blob(payload, payload_len), TAG, "build blob");
    esp_fill_random(nonce, 16);
    esp_err_t err = compute_hmac(nonce, 16, *payload, *payload_len, signature);
    if (err != ESP_OK) {
        free(*payload);
        *payload = NULL;
        *payload_len = 0;
    }
    return err;
}

esp_err_t species_profiles_import_secure(const uint8_t *payload, size_t payload_len, const uint8_t nonce[16], const uint8_t signature[32])
{
    if (!payload || payload_len == 0 || !nonce || !signature) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(load_state(), TAG, "state load");
    uint8_t computed[32];
    ESP_RETURN_ON_ERROR(compute_hmac(nonce, 16, payload, payload_len, computed), TAG, "hmac");
    if (!constant_time_equals(signature, computed, sizeof(computed))) {
        return ESP_ERR_INVALID_CRC;
    }
    custom_storage_t storage = {0};
    esp_err_t err = parse_custom_blob(payload, payload_len, &storage);
    if (err != ESP_OK) {
        custom_storage_free(&storage);
        return err;
    }
    ESP_RETURN_ON_ERROR(adopt_custom_storage(&storage), TAG, "import adopt");
    custom_storage_free(&storage);
    ESP_RETURN_ON_ERROR(persist_custom(), TAG, "import persist");
    if (s_active_key[0] != '\0') {
        const species_profile_t *builtin = find_builtin(s_active_key);
        if (!builtin) {
            custom_profile_internal_t *custom = find_custom(s_active_key, NULL);
            if (!custom) {
                s_active_key[0] = '\0';
            }
        }
    }
    if (s_active_key[0] == '\0' && s_builtin_count > 0) {
        strlcpy(s_active_key, s_builtin_profiles[0].key, sizeof(s_active_key));
    }
    return ESP_OK;
}

void species_profiles_reset(void)
{
    free_custom_profiles();
    free_schedule_pool();
    free_builtin_profiles();
    if (s_nvs) {
        nvs_close(s_nvs);
        s_nvs = 0;
    }
    s_loaded = false;
    s_secret_loaded = false;
    memset(s_active_key, 0, sizeof(s_active_key));
    mbedtls_platform_zeroize(s_import_secret, sizeof(s_import_secret));
}
