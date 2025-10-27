#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "include/config.h"
#include "drivers/i2c_bus.h"
#include "drivers/tca9548a.h"
#include "drivers/pcf8574.h"
#include "drivers/ds3231.h"
#include "drivers/ssr.h"
#include "drivers/fans.h"
#include "drivers/dome_i2c.h"
#include "net/wifi.h"
#include "net/httpd.h"
#include "drivers/alarms.h"

static const char *TAG = "CTRL_APP";

void app_main(void)
{
    esp_err_t err;

    // NVS init
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "NVS initialized");

    // Basic GPIOs
    gpio_reset_pin(LED_STATUS_GPIO);
    gpio_set_direction(LED_STATUS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_STATUS_GPIO, 0);

    // I2C master init
    ESP_ERROR_CHECK(i2c_bus_init(I2C_NUM_0, CTRL_I2C_SDA, CTRL_I2C_SCL, 400000));
    ESP_LOGI(TAG, "I2C master ready");

    // Optionally switch TCA9548A channels if present (not mandatory here)
    tca9548a_select(I2C_NUM_0, 0x70, 0x01); // enable channel 0
    ESP_LOGI(TAG, "TCA9548A channel 0 select (if present)");

    // RTC read (if present)
    ds3231_time_t now = {0};
    if (ds3231_get_time(I2C_NUM_0, 0x68, &now) == ESP_OK) {
        ESP_LOGI(TAG, "RTC %04d-%02d-%02d %02d:%02d:%02d", now.year, now.month, now.day, now.hour, now.min, now.sec);
    } else {
        ESP_LOGW(TAG, "RTC DS3231 not found");
    }

    // SSR, FAN init (stubs)
    ssr_init();
    fans_init();

    // Bring Wi‑Fi + HTTP up (stubs)
    wifi_start_apsta("terrarium-s3", "terrarium123");
    httpd_start_basic();

    // Start alarms task (buzzer patterns)
    alarms_start();

    // Probe Dome via I2C, read STATUS
    uint8_t status = 0xFF;
    if (dome_read_reg(I2C_NUM_0, DOME_I2C_ADDR, 0x00, &status, 1) == ESP_OK) {
        ESP_LOGI(TAG, "Dome STATUS: 0x%02X", status);
    } else {
        ESP_LOGW(TAG, "Dome not responding at 0x%02X", DOME_I2C_ADDR);
    }

    // Blink LED forever
    while (1) {
        gpio_set_level(LED_STATUS_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(LED_STATUS_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}
