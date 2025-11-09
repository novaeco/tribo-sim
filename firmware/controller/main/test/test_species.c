#include "unity.h"
#include "nvs_flash.h"
#include "species_profiles.h"
#include "drivers/climate.h"

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_erase());
        err = nvs_flash_init();
    }
    TEST_ASSERT_EQUAL(ESP_OK, err);
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
    nvs_flash_deinit();
}
