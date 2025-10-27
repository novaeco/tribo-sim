#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"   // + ajout pour ledc_*
#include "esp_timer.h"

#include "include/config.h"
#include "drivers/i2c_bus.h"
#include "drivers/pcf8574.h"
#include "drivers/ds3231.h"
#include "drivers/ssr.h"
#include "drivers/fans.h"
#include "drivers/dome_i2c.h"
#include "drivers/sensors.h"
#include "drivers/climate.h"
#include "drivers/calib.h"
#include "net/wifi.h"
#include "net/httpd.h"
#include "drivers/alarms.h"
#include "drivers/dome_bus.h"

static const char *TAG = "CTRL_APP";
static const uint8_t k_dome_channels[] = {
#if TCA_PRESENT
    TCA_CH_DOME0,
#else
    0
#endif
};

static bool pick_temperature(const terra_sensors_t *s, float *value)
{
    if (!s || !value) {
        return false;
    }
    if (s->sht31_present) {
        *value = s->sht31_t_c;
        return true;
    }
    if (s->sht21_present) {
        *value = s->sht21_t_c;
        return true;
    }
    if (s->bme_present) {
        *value = s->bme_t_c;
        return true;
    }
    if (s->t1_present) {
        *value = s->t1_c;
        return true;
    }
    if (s->t2_present) {
        *value = s->t2_c;
        return true;
    }
    return false;
}

static bool pick_humidity(const terra_sensors_t *s, float *value)
{
    if (!s || !value) {
        return false;
    }
    if (s->sht31_present) {
        *value = s->sht31_rh;
        return true;
    }
    if (s->sht21_present) {
        *value = s->sht21_rh;
        return true;
    }
    if (s->bme_present) {
        *value = s->bme_rh;
        return true;
    }
    return false;
}

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

static void sensors_task(void *arg)
{
    (void)arg;
    SemaphoreHandle_t lock = climate_measurement_mutex();
    const TickType_t period = pdMS_TO_TICKS(2000);
    while (true) {
        terra_sensors_t sensors = {0};
        sensors_read(&sensors);
        climate_state_t state;
        bool has_state = climate_get_state(&state);
        float temp = 0.0f;
        float hum = 0.0f;
        bool has_temp = pick_temperature(&sensors, &temp);
        bool has_hum = pick_humidity(&sensors, &hum);
        climate_measurement_t measurement = {
            .sensors = sensors,
            .temp_drift_c = NAN,
            .humidity_drift_pct = NAN,
            .timestamp_ms = esp_timer_get_time() / 1000
        };
        if (has_state) {
            if (has_temp) {
                measurement.temp_drift_c = temp - state.temp_setpoint_c;
            }
            if (has_hum) {
                measurement.humidity_drift_pct = hum - state.humidity_setpoint_pct;
            }
        }
        if (lock && xSemaphoreTake(lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            climate_measurement_set_locked(&measurement);
            xSemaphoreGive(lock);
        }
        vTaskDelay(period);
    }
}

static void actuators_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(1000);
    while (true) {
        terra_sensors_t sensors = {0};
        climate_measurement_t meas;
        if (climate_measurement_get(&meas)) {
            sensors = meas.sensors;
        } else {
            sensors_read(&sensors);
        }

        int minute_of_day = 0;
        ds3231_time_t rtc_now = {0};
        if (ds3231_get_time(I2C_NUM_0, 0x68, &rtc_now) == ESP_OK) {
            minute_of_day = rtc_now.hour * 60 + rtc_now.min;
        } else {
            minute_of_day = (int)((xTaskGetTickCount() / pdMS_TO_TICKS(60000)) % 1440);
        }

        climate_state_t state = {0};
        climate_tick(&sensors, minute_of_day, &state);

        ssr_set(0, state.heater_on);
        ssr_set(1, state.lights_on);

        esp_err_t fan_err = fans_set_pwm(0, state.fan_pwm_percent);
        if (fan_err != ESP_OK) {
            ESP_LOGW(TAG, "fans_set_pwm channel 0 failed: %s", esp_err_to_name(fan_err));
        }
        fan_err = fans_set_pwm(1, state.fan_pwm_percent);
        if (fan_err != ESP_OK) {
            ESP_LOGW(TAG, "fans_set_pwm channel 1 failed: %s", esp_err_to_name(fan_err));
        }

        float allowed_uvi = state.uvi_target;
        float k = 0.0f;
        float calibration_uvi_max = 0.0f;
        if (calib_get_uvb(&k, &calibration_uvi_max) == ESP_OK && calibration_uvi_max > 0.0f) {
            allowed_uvi = fminf(allowed_uvi, calibration_uvi_max);
        }
        if (allowed_uvi < 0.0f) {
            allowed_uvi = 0.0f;
        }
        float duty_pm = 0.0f;
        if (allowed_uvi > 0.0f) {
            if (uvb_duty_from_uvi(allowed_uvi, &duty_pm) != 0) {
                duty_pm = 0.0f;
            }
        }
        int uvb_pm = (int)fminf(duty_pm, 10000.0f);
        if (uvb_pm < 0) {
            uvb_pm = 0;
        }
        int uva_pm = state.lights_on ? 6000 : 0;
        uint16_t cct_day = state.lights_on ? 9000 : 0;
        uint16_t cct_warm = state.lights_on ? 2000 : 0;
        uint8_t uvb_reg = (uint8_t)(uvb_pm / 100);
        uint8_t uva_reg = (uint8_t)(uva_pm / 100);
        uint8_t uvb_period = 60;
        uint8_t uvb_duty = (uint8_t)(uvb_pm / 100);
        if (uvb_pm > 0 && uvb_duty == 0) {
            uvb_duty = 1;
        }
        if (uvb_duty > 255) {
            uvb_duty = 255;
        }
        uint8_t sky = state.lights_on ? 1 : 0;
        uint8_t payload[4] = {
            (uint8_t)(cct_day & 0xFF),
            (uint8_t)((cct_day >> 8) & 0xFF),
            (uint8_t)(cct_warm & 0xFF),
            (uint8_t)((cct_warm >> 8) & 0xFF)
        };

        for (size_t i = 0; i < sizeof(k_dome_channels) / sizeof(k_dome_channels[0]); ++i) {
            uint8_t mask = k_dome_channels[i];
#if TCA_PRESENT
            if (mask == 0) {
                continue;
            }
            if (dome_bus_select(mask) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to select dome channel mask 0x%02X", mask);
                continue;
            }
#endif
            if (dome_bus_write(0x02, payload, sizeof(payload)) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write CCT payload to dome");
            }
            if (dome_bus_write(0x06, &uva_reg, 1) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write UVA setpoint");
            }
            if (dome_bus_write(0x07, &uvb_reg, 1) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write UVB setpoint");
            }
            if (dome_bus_write(0x0B, &uvb_period, 1) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write UVB period");
            }
            if (dome_bus_write(0x0C, &uvb_duty, 1) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write UVB duty");
            }
            if (dome_bus_write(0x08, &sky, 1) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write sky mode");
            }
        }

        vTaskDelay(period);
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

    ESP_ERROR_CHECK(climate_init());
    ESP_ERROR_CHECK(calib_init());

    // Basic GPIOs
    gpio_reset_pin(LED_STATUS_GPIO);
    gpio_set_direction(LED_STATUS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_STATUS_GPIO, 0);

    // I2C master init
    ESP_ERROR_CHECK(i2c_bus_init(I2C_NUM_0, CTRL_I2C_SDA, CTRL_I2C_SCL, 400000));
    ESP_LOGI(TAG, "I2C master ready");

    // Default dome channel selection if present
    esp_err_t dome_err = dome_bus_select(TCA_CH_DOME0);
    if (dome_err != ESP_OK) {
        ESP_LOGW(TAG, "dome_bus_select default channel failed: %s", esp_err_to_name(dome_err));
    }

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

    sensors_init();

    // Spawn climate regulation tasks
    if (xTaskCreatePinnedToCore(sensors_task, "sensors_task", 4096, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start sensors task");
    }
    if (xTaskCreatePinnedToCore(actuators_task, "actuators_task", 4096, NULL, 6, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start actuators task");
    }

    // Bring Wi-Fi + HTTP up (stubs)
    wifi_start_apsta("terrarium-s3", "terrarium123");
    httpd_start_basic();

    // Start alarms task (buzzer patterns)
    alarms_start();

    // Button long-press task (rearm: clear BUS_LOSS degraded + unmute)
    xTaskCreatePinnedToCore(btn_rearm_task, "btn_rearm", 3072, NULL, 3, NULL, 1);

    // Probe Dome via I2C, read STATUS
    uint8_t status = 0xFF;
    if (dome_bus_read(0x00, &status, 1) == ESP_OK) {
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
