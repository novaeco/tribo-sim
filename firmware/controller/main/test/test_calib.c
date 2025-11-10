#include "unity.h"
#include "esp_err.h"
#include "calib.h"
#include "storage.h"

static void init_nvs_storage(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_erase());
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_init());
}

TEST_CASE("calibration init/deinit cycles reuse a single handle", "[calib]")
{
    init_nvs_storage();

    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, calib_init());
    }

    TEST_ASSERT_EQUAL(ESP_OK, calib_set_uvb(1000.0f, 5.0f));

    calib_deinit();

    for (int cycle = 0; cycle < 5; ++cycle) {
        TEST_ASSERT_EQUAL(ESP_OK, calib_init());
        TEST_ASSERT_EQUAL(ESP_OK, calib_set_uvb_uvi_max(1.0f + 0.1f * cycle));
        TEST_ASSERT_EQUAL(ESP_OK, calib_set_uvb(800.0f + 50.0f * cycle, 4.0f + 0.5f * cycle));
        calib_deinit();
    }

    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
}

TEST_CASE("calibration setters guard against uninitialised state", "[calib]")
{
    calib_deinit();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, calib_set_uvb(500.0f, 2.0f));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, calib_set_uvb_uvi_max(1.0f));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, calib_get_uvb(NULL, NULL));
}
