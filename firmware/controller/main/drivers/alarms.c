#include "alarms.h"
#include "include/config.h"
#include "include/dome_regs.h"
#include "drivers/dome_bus.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdbool.h>

static const char *TAG = "ALARMS";

static portMUX_TYPE s_mute_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_muted = false;

static StaticEventGroup_t s_alarm_events_storage;
static EventGroupHandle_t s_alarm_events = NULL;

#define ALARM_EVENT_BUZZER_READY BIT0

static bool s_buzzer_ready = false;

__attribute__((weak)) esp_err_t alarms_gpio_reset_pin(gpio_num_t gpio_num)
{
    return gpio_reset_pin(gpio_num);
}

__attribute__((weak)) esp_err_t alarms_ledc_timer_config(const ledc_timer_config_t *config)
{
    return ledc_timer_config(config);
}

__attribute__((weak)) esp_err_t alarms_ledc_channel_config(const ledc_channel_config_t *config)
{
    return ledc_channel_config(config);
}

__attribute__((weak)) esp_err_t alarms_ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
    return ledc_set_duty(speed_mode, channel, duty);
}

__attribute__((weak)) esp_err_t alarms_ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
    return ledc_update_duty(speed_mode, channel);
}

static esp_err_t buzzer_apply_duty(uint32_t duty)
{
    if (!s_buzzer_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = alarms_ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty failed: %s", esp_err_to_name(err));
        return err;
    }
    err = alarms_ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t buzzer_init(void)
{
    esp_err_t err = alarms_gpio_reset_pin(BUZZER_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_reset_pin failed: %s", esp_err_to_name(err));
        return err;
    }
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_2,
        .freq_hz = 2000, // 2 kHz
        .clk_cfg = LEDC_AUTO_CLK
    };
    err = alarms_ledc_timer_config(&t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }
    ledc_channel_config_t c = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_7,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_2,
        .duty = 0,
        .hpoint = 0
    };
    err = alarms_ledc_channel_config(&c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }
    s_buzzer_ready = true;
    if (s_alarm_events) {
        xEventGroupSetBits(s_alarm_events, ALARM_EVENT_BUZZER_READY);
    }
    return buzzer_apply_duty(0);
}

static void buzzer_on(void)
{
    if (buzzer_apply_duty(512) != ESP_OK) {
        ESP_LOGW(TAG, "Skipping buzzer_on due to uninitialized LEDC");
    }
}

static void buzzer_off(void)
{
    if (buzzer_apply_duty(0) != ESP_OK) {
        ESP_LOGW(TAG, "Skipping buzzer_off due to uninitialized LEDC");
    }
}

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

    if (!s_alarm_events) {
        s_alarm_events = xEventGroupCreateStatic(&s_alarm_events_storage);
        if (!s_alarm_events) {
            ESP_LOGE(TAG, "failed to create alarms event group");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t buzzer_err = buzzer_init();
    if (buzzer_err != ESP_OK) {
        return buzzer_err;
    }

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

bool alarms_wait_ready(TickType_t ticks_to_wait)
{
    EventGroupHandle_t events = s_alarm_events;
    if (!events) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(events, ALARM_EVENT_BUZZER_READY, pdFALSE, pdTRUE, ticks_to_wait);
    return (bits & ALARM_EVENT_BUZZER_READY) != 0;
}

static void alarms_task(void* arg){
    (void)arg;
    (void)alarms_wait_ready(portMAX_DELAY);
    int tick = 0;
    uint32_t prev_alarm_mask = 0;
    while (1){
        // Read dome status
        uint8_t st=0;
        dome_bus_read(DOME_REG_STATUS, &st, 1);
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
