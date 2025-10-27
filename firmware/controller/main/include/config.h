#pragma once

// === GPIO mapping (controller) ===
#define CTRL_I2C_SDA    8
#define CTRL_I2C_SCL    9
#define CTRL_1W_BUS1   14
#define CTRL_1W_BUS2   21
#define SSR1_GPIO      10
#define SSR2_GPIO      11
#define SSR3_GPIO      12
#define SSR4_GPIO      13
#define FAN1_PWM_GPIO   4
#define FAN1_TACH_GPIO 16
#define FAN2_PWM_GPIO   5
#define FAN2_TACH_GPIO 15
#define BUZZER_GPIO     6
#define LED_STATUS_GPIO 7
#define BTN_USER_GPIO   1
#define DOME_I2C_ADDR  0x3A

// TCA9548A routing
#define TCA_ADDR        0x70
#define TCA_PRESENT     1
#define TCA_CH_SENSORS  0x02
#define TCA_CH_DOME0    0x01

// === CLIMATE bounds (pour /api/climate & validations JSON) ===
#define CLIMATE_TEMP_MIN   10.0
#define CLIMATE_TEMP_MAX   45.0
#define CLIMATE_HUM_MIN    10.0
#define CLIMATE_HUM_MAX    100.0
#define CLIMATE_HYST_MIN   0.0
#define CLIMATE_HYST_MAX   10.0
#define CLIMATE_UVI_MIN    0.0
#define CLIMATE_UVI_MAX    15.0
