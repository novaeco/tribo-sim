#include "unity.h"

#include <math.h>

#include "storage.h"
#include "drivers/climate.h"

TEST_CASE("climate heater fail-safe engages after missing temperature", "[climate]")
{
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_init());
    TEST_ASSERT_EQUAL(ESP_OK, climate_init());

    terra_sensors_t sensors = {0};
    sensors.sht31_present = true;
    sensors.sht31_t_c = 20.0f; // Below day setpoint to enable heater
    sensors.sht31_rh = 50.0f;

    climate_state_t state = {0};
    climate_tick(&sensors, 8 * 60, &state);
    TEST_ASSERT_TRUE_MESSAGE(state.heater_on, "heater should enable with low temperature");

    sensors.sht31_present = false; // simulate sensor loss
    for (int i = 0; i < 2; ++i) {
        climate_tick(&sensors, 8 * 60 + i + 1, &state);
    }
    TEST_ASSERT_TRUE_MESSAGE(state.heater_on, "heater should remain on until fail-safe threshold");

    climate_tick(&sensors, 8 * 60 + 3, &state);
    TEST_ASSERT_FALSE_MESSAGE(state.heater_on, "heater must turn off after fail-safe triggers");
    TEST_ASSERT_TRUE(isnan(state.temp_error_c));

    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
}

