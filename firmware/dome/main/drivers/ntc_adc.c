#include "ntc_adc.h"

#include <math.h>
#include <float.h>

#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"

#include "include/config.h"

#ifndef DOME_NTC_OVERSAMPLE
#define DOME_NTC_OVERSAMPLE 8
#endif

#if DOME_NTC_OVERSAMPLE <= 0
#error "DOME_NTC_OVERSAMPLE must be > 0"
#endif

#define TAG "ntc_adc"
#define ADC_RAW_FULL_SCALE 4095.0f
#define KELVIN_OFFSET 273.15f
#define T0_KELVIN 298.15f

static adc_oneshot_unit_handle_t s_adc1 = NULL;
static adc_cali_handle_t s_cali = NULL;

static void ntc_adc_ensure_init(void)
{
    if (!s_adc1) {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = ADC_UNIT_1,
        };
        esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(err));
            s_adc1 = NULL;
            return;
        }

        adc_oneshot_chan_cfg_t cfg = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_11,
        };
        err = adc_oneshot_config_channel(s_adc1, DOME_NTC_ADC_CH, &cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(err));
            adc_oneshot_del_unit(s_adc1);
            s_adc1 = NULL;
            return;
        }
    }

    if (!s_cali && s_adc1) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali);
        if (err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Curve-fitting calibration not supported, falling back to raw readings");
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init ADC calibration: %s", esp_err_to_name(err));
        }
    }
}

static bool ntc_adc_read_voltage(float *out_voltage_v)
{
    ntc_adc_ensure_init();
    if (!s_adc1) {
        return false;
    }

    int32_t accum_mv = 0;
    int32_t accum_raw = 0;

    for (int i = 0; i < DOME_NTC_OVERSAMPLE; ++i) {
        if (s_cali) {
            int result_mv = 0;
            esp_err_t err = adc_oneshot_get_calibrated_result(s_adc1, DOME_NTC_ADC_CH, s_cali, &result_mv);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ADC calibrated read failed: %s", esp_err_to_name(err));
                return false;
            }
            accum_mv += result_mv;
        } else {
            int raw = 0;
            esp_err_t err = adc_oneshot_read(s_adc1, DOME_NTC_ADC_CH, &raw);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ADC raw read failed: %s", esp_err_to_name(err));
                return false;
            }
            accum_raw += raw;
        }
    }

    if (s_cali) {
        float avg_mv = (float)accum_mv / (float)DOME_NTC_OVERSAMPLE;
        *out_voltage_v = avg_mv / 1000.0f;
    } else {
        float avg_raw = (float)accum_raw / (float)DOME_NTC_OVERSAMPLE;
        *out_voltage_v = (avg_raw / ADC_RAW_FULL_SCALE) * (DOME_NTC_SUPPLY_MV / 1000.0f);
    }

    return true;
}

float ntc_adc_read_celsius(void)
{
    float v_ntc = 0.0f;
    if (!ntc_adc_read_voltage(&v_ntc)) {
        return NAN;
    }

    const float v_supply = DOME_NTC_SUPPLY_MV / 1000.0f;
    if (v_ntc <= 0.0f || v_ntc >= v_supply) {
        ESP_LOGE(TAG, "ADC voltage out of range: %.3f V (supply %.3f V)", v_ntc, v_supply);
        return NAN;
    }

    const float resistance = DOME_NTC_PULLUP_OHMS * (v_ntc / (v_supply - v_ntc));
    if (resistance <= 0.0f || !isfinite(resistance)) {
        ESP_LOGE(TAG, "Invalid NTC resistance computed: %.3f ohms", resistance);
        return NAN;
    }

    const float ratio = resistance / DOME_NTC_R25_OHMS;
    if (ratio <= 0.0f || !isfinite(ratio)) {
        ESP_LOGE(TAG, "Invalid NTC ratio computed: %.6f", ratio);
        return NAN;
    }

    const float inv_t = (1.0f / T0_KELVIN) + (logf(ratio) / DOME_NTC_BETA_K);
    if (inv_t <= 0.0f || !isfinite(inv_t)) {
        ESP_LOGE(TAG, "Invalid inverse temperature value: %.6e", inv_t);
        return NAN;
    }

    const float temperature_c = (1.0f / inv_t) - KELVIN_OFFSET;
    return temperature_c;
}
