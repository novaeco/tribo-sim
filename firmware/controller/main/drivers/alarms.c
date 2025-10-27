#include "alarms.h"
#include "include/config.h"
#include "drivers/dome_bus.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static void alarms_task(void* arg){
    buzzer_init();
    int tick = 0;
    while (1){
        // Read dome status
        uint8_t st=0;
        dome_bus_read(0x00, &st, 1);
        bool degraded = dome_bus_is_degraded();
        bool interlock = (st & (1<<5))!=0;
        bool ot_soft   = (st & (1<<0))!=0;

        // Priority: INTERLOCK > degraded > OT soft
        if (interlock){
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

        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void alarms_start(void){
    xTaskCreatePinnedToCore(alarms_task, "alarms", 3072, NULL, 4, NULL, 1);
}
