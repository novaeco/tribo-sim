#include "mock_ledc.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

#ifndef LEDC_CHANNEL_MAX
#define LEDC_CHANNEL_MAX 8
#endif

static bool s_timer_configured = false;
static bool s_channel_configured[LEDC_CHANNEL_MAX];
static int s_set_duty_errors = 0;

void mock_ledc_reset(void)
{
    s_timer_configured = false;
    memset(s_channel_configured, 0, sizeof(s_channel_configured));
    s_set_duty_errors = 0;
}

bool mock_ledc_timer_configured(void)
{
    return s_timer_configured;
}

bool mock_ledc_channel_configured(int channel)
{
    if (channel < 0 || channel >= LEDC_CHANNEL_MAX) {
        return false;
    }
    return s_channel_configured[channel];
}

int mock_ledc_get_set_duty_errors(void)
{
    return s_set_duty_errors;
}

esp_err_t alarms_gpio_reset_pin(gpio_num_t gpio_num)
{
    (void)gpio_num;
    return ESP_OK;
}

esp_err_t alarms_ledc_timer_config(const ledc_timer_config_t *config)
{
    (void)config;
    s_timer_configured = true;
    return ESP_OK;
}

esp_err_t alarms_ledc_channel_config(const ledc_channel_config_t *config)
{
    if (!s_timer_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    int channel = (int)config->channel;
    if (channel < 0 || channel >= LEDC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_channel_configured[channel] = true;
    return ESP_OK;
}

esp_err_t alarms_ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
    (void)speed_mode;
    (void)duty;
    int ch = (int)channel;
    if (ch < 0 || ch >= LEDC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_channel_configured[ch]) {
        s_set_duty_errors++;
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t alarms_ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
    (void)speed_mode;
    (void)channel;
    return ESP_OK;
}
