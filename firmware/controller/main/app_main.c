#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"   // + ajout pour ledc_*

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
#include "drivers/dome_bus.h"

static const char *TAG = "CTRL_APP";

// --- tâche C (remplace la lambda C++) ---
static void btn_rearm_task(void *arg){
    gpio_reset_pin(BTN_USER_GPIO);
    gpio_set_direction(BTN_USER_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_USER_GPIO, GPIO_PULLUP_ONLY);
    const int LONG_MS = 2000;
    int count = 0;
    for(;;){
        int lvl = gpio_get_level(BTN_USER_GPIO); // 1=idle (pull-up), 0=pressed
        if (lvl == 0){ count += 10; } else { if (count>0) count -= 10; if (count<0) count=0; }
        if (count >= LONG_MS){
            // Rearm: clear degraded + unmute
            dome_bus_clear_degraded();
            esp_err_t mute_err = alarms_set_mute(false);
            if (mute_err == ESP_OK){
                // Feedback: quick 3 beeps
                for (int i=0;i<3;i++){
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 512);
                    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
                    vTaskDelay(pdMS_TO_TICKS(120));
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0);
                    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
                    vTaskDelay(pdMS_TO_TICKS(120));
                }
            } else {
                ESP_LOGE(TAG, "Failed to clear alarm mute: %s", esp_err_to_name(mute_err));
            }
            count = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

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

    ESP_ERROR_CHECK(alarms_init());
    ESP_LOGI(TAG, "Alarms restored");

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

    // Bring Wi-Fi + HTTP up (stubs)
    wifi_start_apsta("terrarium-s3", "terrarium123");
    httpd_start_basic();

    // Start alarms task (buzzer patterns)
    alarms_start();

    // Button long-press task (rearm: clear BUS_LOSS degraded + unmute)
    xTaskCreatePinnedToCore(btn_rearm_task, "btn_rearm", 3072, NULL, 3, NULL, 1);

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
