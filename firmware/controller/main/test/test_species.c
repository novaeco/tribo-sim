#include "unity.h"
#include "storage.h"
#include "species_profiles.h"
#include "drivers/climate.h"

static void init_nvs(void)
{
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
}
