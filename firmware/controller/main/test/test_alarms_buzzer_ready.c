#include "unity.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "drivers/alarms.h"
#include "driver/ledc.h"
#include "storage.h"

#include "mock_ledc.h"

TEST_CASE("alarms waits for buzzer readiness before LEDC access", "[alarms][buzzer]")
{
    mock_ledc_reset();

    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_erase());
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_init());

    TEST_ASSERT_FALSE(alarms_wait_ready(0));

    esp_err_t init_err = alarms_init();
    TEST_ASSERT_EQUAL(ESP_OK, init_err);

    TEST_ASSERT_TRUE(alarms_wait_ready(pdMS_TO_TICKS(1)));
    TEST_ASSERT_TRUE(mock_ledc_timer_configured());
    TEST_ASSERT_TRUE(mock_ledc_channel_configured(LEDC_CHANNEL_7));

    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 512));
        TEST_ASSERT_EQUAL(ESP_OK, ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7));
        vTaskDelay(pdMS_TO_TICKS(1));
        TEST_ASSERT_EQUAL(ESP_OK, ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0));
        TEST_ASSERT_EQUAL(ESP_OK, ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7));
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    TEST_ASSERT_EQUAL(0, mock_ledc_get_set_duty_errors());

    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
}
