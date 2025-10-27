#include "ntc_adc.h"
#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "include/config.h"
float ntc_adc_read_celsius(void){
    static adc_oneshot_unit_handle_t adc1 = NULL;
    if (!adc1){
        adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = ADC_UNIT_1 };
        adc_oneshot_new_unit(&init_cfg, &adc1);
        adc_oneshot_chan_cfg_t cfg = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_11 };
        adc_oneshot_config_channel(adc1, DOME_NTC_ADC_CH, &cfg);
    }
    int raw = 0;
    adc_oneshot_read(adc1, DOME_NTC_ADC_CH, &raw);
    // Very rough placeholder conversion
    float voltage = raw / 4095.0f * 3.3f;
    float temp_c = 25.0f + (1.43f - voltage) * 30.0f; // placeholder mapping
    return temp_c;
}
