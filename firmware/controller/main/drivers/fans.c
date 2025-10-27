#include "fans.h"
#include "driver/ledc.h"
#include "include/config.h"

static const ledc_channel_t k_fan_channels[] = {
    LEDC_CHANNEL_0,
    LEDC_CHANNEL_1,
};

void fans_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 25000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c1 = {
        .gpio_num = FAN1_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config_t c2 = {
        .gpio_num = FAN2_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&c1);
    ledc_channel_config(&c2);
}

esp_err_t fans_set_pwm(uint8_t channel, uint8_t duty_percent)
{
    if (channel >= (sizeof(k_fan_channels) / sizeof(k_fan_channels[0]))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    uint32_t duty = ((1U << LEDC_TIMER_10_BIT) - 1U);
    duty = (duty * duty_percent) / 100U;
    ledc_channel_t ledc_channel = k_fan_channels[channel];
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, ledc_channel, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, ledc_channel);
}
