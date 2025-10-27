#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "include/config.h"
#include "include/regs.h"
#include "drivers/i2c_slave_if.h"
#include "drivers/ledc_cc.h"
#include "drivers/ws2812_rmt.h"
#include "drivers/fan_pwm.h"
#include "drivers/ntc_adc.h"

static const char *TAG = "DOME_APP";

static uint8_t regfile[256]; // I2C register space
static float t_c = 25.0f;
static bool uvb_pulse_state = false;

static inline uint16_t rd16(int lo_reg){ return (uint16_t)regfile[lo_reg] | ((uint16_t)regfile[lo_reg+1]<<8); }

static void dome_assert_int(bool assert){
    gpio_set_direction(DOME_INT_GPIO, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(DOME_INT_GPIO, assert ? 0 : 1); // open-drain: 0=assert, 1=release
}

static volatile bool interlock_tripped = false;

static void IRAM_ATTR interlock_isr(void* arg){
    interlock_tripped = true;
}

static void interlock_init(void){
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL<<DOME_INTERLOCK_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(DOME_INTERLOCK_GPIO, interlock_isr, NULL);
}

static bool interlock_active(void){
    int level = gpio_get_level(DOME_INTERLOCK_GPIO);
    return level == 0; // active-low
}

static bool therm_hard_active(void){
#if DOME_THERM_GPIO >= 0
    gpio_config_t cfg = { .pin_bit_mask = 1ULL<<DOME_THERM_GPIO, .mode=GPIO_MODE_INPUT, .pull_up_en=GPIO_PULLUP_ENABLE };
    gpio_config(&cfg);
    return gpio_get_level(DOME_THERM_GPIO) == 0;
#else
    return false;
#endif
}

static void dome_apply_outputs(bool force_uv_off){
    int ch1 = rd16(DOME_REG_CCT1_L); // 0..10000
    int ch2 = rd16(DOME_REG_CCT2_L);
    int uva = regfile[DOME_REG_UVA_SET]; if (uva <= 100) uva *= 100;
    int uvb = regfile[DOME_REG_UVB_SET]; if (uvb <= 100) uvb *= 100;

    int uva_clamp = regfile[DOME_REG_UVA_CLAMP] ? regfile[DOME_REG_UVA_CLAMP]*100 : DOME_UVA_CLAMP_PM_DEFAULT;
    int uvb_clamp = regfile[DOME_REG_UVB_CLAMP] ? regfile[DOME_REG_UVB_CLAMP]    : DOME_UVB_CLAMP_PM_DEFAULT;

    uint8_t st = regfile[DOME_REG_STATUS] & ~(ST_UVA_LIMIT|ST_UVB_LIMIT|ST_INTERLOCK|ST_THERM_HARD);

    if (force_uv_off || interlock_active()) {
        st |= ST_INTERLOCK;
        uva = 0; uvb = 0;
    }
    if (therm_hard_active()) {
        st |= ST_THERM_HARD;
        uva = 0; uvb = 0;
    }

    if (uva > uva_clamp){ uva = uva_clamp; st |= ST_UVA_LIMIT; }
    if (uvb > uvb_clamp){ uvb = uvb_clamp; st |= ST_UVB_LIMIT; }

    ledc_cc_set(0, ch1);
    ledc_cc_set(1, ch2);
    ledc_cc_set(2, uva);
    // UVB pulse: if interlock/therm -> off anyway via force_uv_off path
    ledc_cc_set(3, uvb);

    regfile[DOME_REG_STATUS] = st;
    regfile[DOME_REG_TLM_T_HEAT] = (uint8_t)(t_c + 0.5f);

    if (st & (ST_UVA_LIMIT|ST_UVB_LIMIT|ST_INTERLOCK|ST_THERM_HARD|ST_OT)) dome_assert_int(true);
    else dome_assert_int(false);
}

static void telemetry_task(void* arg){
    while (1){
        t_c = ntc_adc_read_celsius();
        if (t_c >= DOME_OT_SOFT_C) {
            regfile[DOME_REG_STATUS] |= ST_OT;
            dome_apply_outputs(true); // force UV off
        } else {
            regfile[DOME_REG_STATUS] &= ~ST_OT;
            dome_apply_outputs(false);
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 50 ms -> <100 ms reaction to interlock via periodic + ISR flag
        if (interlock_tripped){
            interlock_tripped = false;
            dome_apply_outputs(true); // immediate cut
        }
    }
}

void app_main(void){
    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // INT open-drain default released
    gpio_config_t io = {.pin_bit_mask = 1ULL<<DOME_INT_GPIO, .mode = GPIO_MODE_OUTPUT_OD, .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io);
    dome_assert_int(false);

    interlock_init();

    // LEDC channels
    ESP_ERROR_CHECK(ledc_cc_init());
    // Fan PWM
    fan_init(DOME_FAN_PWM);
    // WS2812
    ws2812_init(DOME_WS_GPIO);

    // I2C slave
    ESP_ERROR_CHECK(i2c_slave_if_init(I2C_NUM_0, DOME_I2C_SDA, DOME_I2C_SCL, DOME_I2C_ADDR));
    ESP_LOGI("DOME_APP", "I2C slave ready @0x%02X", DOME_I2C_ADDR);

    // Default registers
    memset(regfile, 0, sizeof(regfile));
    regfile[DOME_REG_UVA_CLAMP] = DOME_UVA_CLAMP_PM_DEFAULT/100;
    regfile[DOME_REG_UVB_CLAMP] = DOME_UVB_CLAMP_PM_DEFAULT; // permille
    regfile[DOME_REG_UVB_PERIOD_S] = 60;
    regfile[DOME_REG_UVB_DUTY_PM]  = 1000;

    xTaskCreatePinnedToCore(telemetry_task, "telemetry", 4096, NULL, 6, NULL, 0);

    // Simple register protocol
    while (1){
        uint8_t buf[32];
        int n = i2c_slave_if_read(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n >= 1){
            uint8_t reg = buf[0];
            if (n == 1){
                uint8_t v = regfile[reg];
                i2c_slave_if_write(&v, 1, pdMS_TO_TICKS(10));
            } else if (n >= 2){
                uint8_t len = buf[1];
                if (len > 0 && (2 + len) <= n){
                    memcpy(&regfile[reg], &buf[2], len);
                    dome_apply_outputs(false);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
