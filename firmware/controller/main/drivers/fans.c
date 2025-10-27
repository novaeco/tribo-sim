#include "fans.h"
#include "driver/ledc.h"
#include "include/config.h"
void fans_init(void){
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 25000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c1 = { .gpio_num=FAN1_PWM_GPIO, .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_0, .intr_type=LEDC_INTR_DISABLE, .timer_sel=LEDC_TIMER_0, .duty=0, .hpoint=0 };
    ledc_channel_config_t c2 = { .gpio_num=FAN2_PWM_GPIO, .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_1, .intr_type=LEDC_INTR_DISABLE, .timer_sel=LEDC_TIMER_0, .duty=0, .hpoint=0 };
    ledc_channel_config(&c1);
    ledc_channel_config(&c2);
}
