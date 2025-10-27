#include "ledc_cc.h"
#include "driver/ledc.h"
#include "include/config.h"
static int chmap[4] = {DOME_CH1_GPIO, DOME_CH2_GPIO, DOME_CH3_GPIO, DOME_CH4_GPIO};
esp_err_t ledc_cc_init(void){
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    for(int i=0;i<4;i++){
        ledc_channel_config_t c = {0};
        c.gpio_num = chmap[i];
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel = (ledc_channel_t)i;
        c.intr_type = LEDC_INTR_DISABLE;
        c.timer_sel = LEDC_TIMER_0;
        c.duty = 0;
        c.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&c));
    }
    return ESP_OK;
}
esp_err_t ledc_cc_set(int ch, int permille){
    if (ch<0 || ch>3) return ESP_ERR_INVALID_ARG;
    int maxd = (1<<12)-1;
    int duty = (permille<0?0:permille>10000?10000:permille) * maxd / 10000;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty));
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
}
