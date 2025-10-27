#include "alarms.h"
#include "include/config.h"
#include "drivers/dome_bus.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdbool.h>

static const char *TAG = "ALARMS";

static portMUX_TYPE s_mute_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_muted = false;

static void buzzer_init(void){
    gpio_reset_pin(BUZZER_GPIO);
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_2,
        .freq_hz = 2000, // 2 kHz
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_7,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_2,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&c);
}
static void buzzer_on(void){ ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 512); ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7); }
static void buzzer_off(void){ ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0); ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7); }

esp_err_t alarms_init(void){
    nvs_handle_t handle;
    esp_err_t err = nvs_open("alarms", NVS_READWRITE, &handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t muted_u8 = 0;
    err = nvs_get_u8(handle, "muted", &muted_u8);
    if (err == ESP_ERR_NVS_NOT_FOUND){
        muted_u8 = 0;
        err = ESP_OK;
    } else if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_get_u8 failed: %s", esp_err_to_name(err));
    }

    nvs_close(handle);

    portENTER_CRITICAL(&s_mute_lock);
    s_muted = muted_u8 != 0;
    portEXIT_CRITICAL(&s_mute_lock);

    if (s_muted){
        buzzer_off();
    }

    return err;
}

bool alarms_get_mute(void){
    portENTER_CRITICAL(&s_mute_lock);
    bool muted = s_muted;
    portEXIT_CRITICAL(&s_mute_lock);
    return muted;
}

esp_err_t alarms_set_mute(bool muted){
    bool current;
    portENTER_CRITICAL(&s_mute_lock);
    current = s_muted;
    portEXIT_CRITICAL(&s_mute_lock);

    if (current == muted){
        if (muted){
            buzzer_off();
        }
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("alarms", NVS_READWRITE, &handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, "muted", muted ? 1 : 0);
    if (err == ESP_OK){
        err = nvs_commit(handle);
    }
    if (err != ESP_OK){
        ESP_LOGE(TAG, "persist mute=%d failed: %s", muted, esp_err_to_name(err));
    }
    nvs_close(handle);

    if (err != ESP_OK){
        return err;
    }

    portENTER_CRITICAL(&s_mute_lock);
    s_muted = muted;
    portEXIT_CRITICAL(&s_mute_lock);

    if (muted){
        buzzer_off();
    }

    return ESP_OK;
}

static void alarms_task(void* arg){
    buzzer_init();
    int tick = 0;
    uint32_t prev_alarm_mask = 0;
    while (1){
        // Read dome status
        uint8_t st=0;
        dome_bus_read(0x00, &st, 1);
        bool degraded = dome_bus_is_degraded();
        bool interlock = (st & (1<<5))!=0;
        bool ot_soft   = (st & (1<<0))!=0;

        uint32_t alarm_mask = (interlock ? 0x1 : 0) |
                              (degraded  ? 0x2 : 0) |
                              (ot_soft   ? 0x4 : 0);
        bool new_alarm_event = (alarm_mask & ~prev_alarm_mask) != 0;

        bool muted = alarms_get_mute();
        bool auto_unmute_failed = false;
        if (muted && new_alarm_event && alarm_mask != 0){
            esp_err_t err = alarms_set_mute(false);
            if (err != ESP_OK){
                ESP_LOGE(TAG, "auto-unmute failed: %s", esp_err_to_name(err));
                auto_unmute_failed = true;
            } else {
                muted = false;
            }
        }

        // Priority: INTERLOCK > degraded > OT soft
        if (muted){
            buzzer_off();
        } else if (interlock){
            // Fast beep ~8 Hz
            if ((tick % 6) < 3) buzzer_on(); else buzzer_off();
        } else if (degraded){
            // Long beep 0.5s every 2s
            if ((tick % 40) < 10) buzzer_on(); else buzzer_off();
        } else if (ot_soft){
            // Triple short beeps every 5s
            int t = tick % 250; // 250 * 50ms = 12.5s cycle
            if ((t<6) || (t>=12 && t<18) || (t>=24 && t<30)) buzzer_on(); else buzzer_off();
        } else {
            buzzer_off();
        }

        prev_alarm_mask = auto_unmute_failed ? 0 : alarm_mask;
        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void alarms_start(void){
    xTaskCreatePinnedToCore(alarms_task, "alarms", 3072, NULL, 4, NULL, 1);
}
