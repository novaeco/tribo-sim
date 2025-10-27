#include "ssr.h"
#include "driver/gpio.h"
#include "include/config.h"
static const int ssr_pins[4] = {SSR1_GPIO, SSR2_GPIO, SSR3_GPIO, SSR4_GPIO};
void ssr_init(void){
    for (int i=0;i<4;i++){ gpio_reset_pin(ssr_pins[i]); gpio_set_direction(ssr_pins[i], GPIO_MODE_OUTPUT); gpio_set_level(ssr_pins[i], 0); }
}
void ssr_set(int idx, int on){ if(idx>=0 && idx<4) gpio_set_level(ssr_pins[idx], on?1:0); }
