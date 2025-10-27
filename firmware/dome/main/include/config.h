#pragma once
#include "driver/adc.h"

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

// NTC ADC channel (example)
#define DOME_NTC_ADC_CH  ADC_CHANNEL_2 /* GPIO2 on ADC1 (example) */

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
