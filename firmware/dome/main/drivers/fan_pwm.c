#include "fan_pwm.h"
#include "driver/ledc.h"
void fan_init(int pwm_gpio){
    ledc_timer_config_t t = {.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,.timer_num=LEDC_TIMER_1,.freq_hz=25000,.clk_cfg=LEDC_AUTO_CLK};
    ledc_timer_config(&t);
    ledc_channel_config_t c = {.gpio_num=pwm_gpio,.speed_mode=LEDC_LOW_SPEED_MODE,.channel=LEDC_CHANNEL_6,.intr_type=LEDC_INTR_DISABLE,.timer_sel=LEDC_TIMER_1,.duty=0,.hpoint=0};
    ledc_channel_config(&c);
}
