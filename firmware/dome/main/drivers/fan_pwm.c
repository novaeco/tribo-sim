#include "fan_pwm.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"

#define FAN_TIMER          LEDC_TIMER_1
#define FAN_CHANNEL        LEDC_CHANNEL_6
#define FAN_SPEED_MODE     LEDC_LOW_SPEED_MODE
#define FAN_PWM_RESOLUTION LEDC_TIMER_10_BIT

static const char *TAG = "FAN_PWM";
static uint16_t s_raw_pwm = 0;
static int s_pwm_gpio = -1;
static int64_t s_last_nonzero_ts = 0;
static uint32_t s_error_count = 0;

static inline void fan_record_error(esp_err_t err, const char *what)
{
    if (err == ESP_OK) {
        return;
    }
    if (s_error_count != UINT32_MAX) {
        ++s_error_count;
    }
    ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
}

void fan_init(int pwm_gpio)
{
    s_pwm_gpio = pwm_gpio;
    ledc_timer_config_t t = {
        .speed_mode = FAN_SPEED_MODE,
        .duty_resolution = FAN_PWM_RESOLUTION,
        .timer_num = FAN_TIMER,
        .freq_hz = 25000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    fan_record_error(ledc_timer_config(&t), "ledc_timer_config");
    ledc_channel_config_t c = {
        .gpio_num = pwm_gpio,
        .speed_mode = FAN_SPEED_MODE,
        .channel = FAN_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = FAN_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    fan_record_error(ledc_channel_config(&c), "ledc_channel_config");
    s_raw_pwm = 0;
    s_last_nonzero_ts = 0;
}

void fan_set_percent(float percent)
{
    if (s_pwm_gpio < 0) {
        return;
    }
    if (percent < 0.0f) {
        percent = 0.0f;
    }
    if (percent > 100.0f) {
        percent = 100.0f;
    }
    uint32_t duty = (uint32_t)((percent / 100.0f) * ((1u << FAN_PWM_RESOLUTION) - 1));
    s_raw_pwm = (uint16_t)duty;
    if (duty > 0) {
        s_last_nonzero_ts = esp_timer_get_time();
    }
    fan_record_error(ledc_set_duty(FAN_SPEED_MODE, FAN_CHANNEL, duty), "ledc_set_duty");
    fan_record_error(ledc_update_duty(FAN_SPEED_MODE, FAN_CHANNEL), "ledc_update_duty");
}

uint16_t fan_get_raw_pwm(void)
{
    return s_raw_pwm;
}

bool fan_is_running(void)
{
    if (s_raw_pwm > 0) {
        return true;
    }
    int64_t since = esp_timer_get_time() - s_last_nonzero_ts;
    return since < 2 * 1000 * 1000; // consider spinning for 2 s after last non-zero command
}

uint32_t fan_get_error_count(void)
{
    return s_error_count;
}

void fan_reset_error_count(void)
{
    s_error_count = 0;
}
