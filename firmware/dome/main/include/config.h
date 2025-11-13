#pragma once
#include "esp_adc/adc_oneshot.h"

// I2C slave pins/address
#define DOME_I2C_SDA   8
#define DOME_I2C_SCL   9
#define DOME_I2C_ADDR  0x3A
#define DOME_INT_GPIO  1 // open-drain INT out (wire to controller with pull-up)

// LEDC channels (CH1 day, CH2 warm, CH3 UVA, CH4 UVB)
#define DOME_CH1_GPIO 10
#define DOME_CH2_GPIO 11
#define DOME_CH3_GPIO 12
#define DOME_CH4_GPIO 13

// WS2812 (one pixel ring demo)
#define DOME_WS_GPIO  18

// Fan
#define DOME_FAN_PWM  4
#define DOME_FAN_TACH 5

// NTC thermistor sensing
#define DOME_NTC_ADC_CH          ADC_CHANNEL_2 /* GPIO2 on ADC1 (example) */
#define DOME_NTC_PULLUP_OHMS     10000.0f      /* Series pull-up resistor */
#define DOME_NTC_R25_OHMS        10000.0f      /* NTC resistance @ 25 °C */
#define DOME_NTC_BETA_K          3950.0f       /* Beta coefficient (Kelvin) */
#define DOME_NTC_SUPPLY_MV       3300.0f       /* Measured rail feeding the divider */
#define DOME_NTC_OVERSAMPLE      8             /* Number of ADC samples to average */

// UVI photodiode sensing (default: analog front-end on ADC1 channel 3 / GPIO3)
#define DOME_UVI_SENSOR_MODE_ADC 0
#define DOME_UVI_SENSOR_MODE_I2C 1
#define DOME_UVI_SENSOR_MODE     DOME_UVI_SENSOR_MODE_ADC
#define DOME_UVI_ADC_CHANNEL     ADC_CHANNEL_3
#define DOME_UVI_ADC_ATTEN       ADC_ATTEN_DB_11
#define DOME_UVI_ADC_OVERSAMPLE  16
#define DOME_UVI_SUPPLY_MV       3300.0f
#define DOME_UVI_RESP_GAIN_UWCM2_PER_V  18.75f  /* µW/cm² per volt (tunable with calibration) */
#define DOME_UVI_RESP_OFFSET_UWCM2      0.0f    /* Offset compensation */
#define DOME_UVI_FILTER_ALPHA           0.18f   /* IIR smoothing coefficient */
#define DOME_UVI_SAMPLE_PERIOD_MS       50      /* Minimum polling period */

// Interlock capot (GPIO input, active-low, pull-up). Choose a safe pin.
#define DOME_INTERLOCK_GPIO 17

// Optional thermostat readback (active-low). If not wired, set to -1.
#define DOME_THERM_GPIO -1

// Default clamps (permille)
#define DOME_UVA_CLAMP_PM_DEFAULT 3000    // 30%
#define DOME_UVB_CLAMP_PM_DEFAULT 500     // 5%

// Temperature thresholds (°C)
#define DOME_OT_SOFT_C  75.0f
#define DOME_OT_HARD_C  85.0f
