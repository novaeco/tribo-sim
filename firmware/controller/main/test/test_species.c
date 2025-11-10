#include "unity.h"
#include "storage.h"
#include "species_profiles.h"
#include "drivers/climate.h"

#include <stdlib.h>
#include <string.h>

static void init_nvs(void)
{
    species_profiles_reset();
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_erase());
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_init());
}

TEST_CASE("species builtin profiles initialise climate", "[species]")
{
    init_nvs();
    TEST_ASSERT_EQUAL(ESP_OK, climate_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_init());
    const char *default_key = "builtin:pogona_vitticeps";
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_apply(default_key));
    char key[32] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_get_active_key(key, sizeof(key)));
    TEST_ASSERT_EQUAL_STRING(default_key, key);
    climate_state_t state;
    TEST_ASSERT_TRUE(climate_get_state(&state));
    TEST_ASSERT_TRUE(state.temp_setpoint_c > 0.0f);
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
    species_profiles_reset();
}

TEST_CASE("species locale fallback resolves labels", "[species]")
{
    init_nvs();
    TEST_ASSERT_EQUAL(ESP_OK, climate_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_init());
    const species_profile_t *profile = species_profiles_builtin(0);
    TEST_ASSERT_NOT_NULL(profile);
    const char *fr = species_profiles_label_for_locale(profile, "fr", "en");
    TEST_ASSERT_NOT_NULL(fr);
    TEST_ASSERT_TRUE(strlen(fr) > 0);
    const char *fallback = species_profiles_label_for_locale(profile, "zz", "en");
    TEST_ASSERT_NOT_NULL(fallback);
    TEST_ASSERT_TRUE(strlen(fallback) > 0);
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
    species_profiles_reset();
}

static climate_schedule_t demo_schedule(void)
{
    climate_schedule_t sched = {0};
    sched.day_start_minute = 420;
    sched.night_start_minute = 1260;
    sched.day.temp_c = 32.0f;
    sched.day.humidity_pct = 35.0f;
    sched.day.temp_hysteresis_c = 1.5f;
    sched.day.humidity_hysteresis_pct = 5.0f;
    sched.day_uvi_max = 6.5f;
    sched.night.temp_c = 22.0f;
    sched.night.humidity_pct = 60.0f;
    sched.night.temp_hysteresis_c = 1.0f;
    sched.night.humidity_hysteresis_pct = 8.0f;
    sched.night_uvi_max = 0.2f;
    return sched;
}

TEST_CASE("species custom metadata persists across reset", "[species]")
{
    init_nvs();
    TEST_ASSERT_EQUAL(ESP_OK, climate_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_init());
    climate_schedule_t sched = demo_schedule();
    species_profile_metadata_t meta = {
        .habitat = "Montane forest",
        .uv_index_category = "High",
        .season_cycle = "Wet/Dry",
        .uv_index_peak = 6.5f,
    };
    char key[32] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_save_custom("Metadata Demo", &sched, &meta, key, sizeof(key)));
    TEST_ASSERT_EQUAL_STRING("custom:metadata_demo", key);
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
    species_profiles_reset();

    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_init());
    TEST_ASSERT_EQUAL_UINT32(1, species_profiles_custom_count());
    species_custom_profile_t loaded = {0};
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_custom_get(0, &loaded));
    TEST_ASSERT_EQUAL_STRING("Metadata Demo", loaded.name);
    TEST_ASSERT_EQUAL_FLOAT(6.5f, loaded.uv_index_peak);
    TEST_ASSERT_EQUAL_STRING("Montane forest", loaded.habitat);
    TEST_ASSERT_EQUAL_STRING("High", loaded.uv_index_category);
    TEST_ASSERT_EQUAL_STRING("Wet/Dry", loaded.season_cycle);
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
    species_profiles_reset();
}

static void prepare_custom_profile(const char *name)
{
    climate_schedule_t sched = demo_schedule();
    species_profile_metadata_t meta = {
        .habitat = "Savannah",
        .uv_index_category = "Very High",
        .season_cycle = "Seasonal",
        .uv_index_peak = 7.0f,
    };
    char key[32];
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_save_custom(name, &sched, &meta, key, sizeof(key)));
}

TEST_CASE("species secure export import roundtrip", "[species]")
{
    init_nvs();
    TEST_ASSERT_EQUAL(ESP_OK, climate_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_init());
    prepare_custom_profile("Roundtrip");
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    uint8_t nonce[16];
    uint8_t signature[32];
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_export_secure(&blob, &blob_len, nonce, signature));
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT(blob_len > 0);
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
    species_profiles_reset();
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_import_secure(blob, blob_len, nonce, signature));
    free(blob);
    TEST_ASSERT_EQUAL_UINT32(1, species_profiles_custom_count());
    species_custom_profile_t restored = {0};
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_custom_get(0, &restored));
    TEST_ASSERT_EQUAL_STRING("Roundtrip", restored.name);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, restored.uv_index_peak);
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
    species_profiles_reset();
}

TEST_CASE("species secure import rejects tampering", "[species]")
{
    init_nvs();
    TEST_ASSERT_EQUAL(ESP_OK, climate_init());
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_init());
    prepare_custom_profile("Tamper");
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    uint8_t nonce[16];
    uint8_t signature[32];
    TEST_ASSERT_EQUAL(ESP_OK, species_profiles_export_secure(&blob, &blob_len, nonce, signature));
    TEST_ASSERT_NOT_NULL(blob);
    blob[0] ^= 0xFF;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, species_profiles_import_secure(blob, blob_len, nonce, signature));
    free(blob);
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
    species_profiles_reset();
}
