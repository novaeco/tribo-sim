#include "uvi_sensor.h"

#include <math.h>
#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"

#include "include/config.h"

#define TAG "uvi_sensor"

#ifndef DOME_UVI_SENSOR_MODE
#define DOME_UVI_SENSOR_MODE DOME_UVI_SENSOR_MODE_ADC
#endif

#ifndef DOME_UVI_SAMPLE_PERIOD_MS
#define DOME_UVI_SAMPLE_PERIOD_MS 40
#endif

#ifndef DOME_UVI_FILTER_ALPHA
#define DOME_UVI_FILTER_ALPHA 0.2f
#endif

#ifndef DOME_UVI_RESP_GAIN_UWCM2_PER_V
#define DOME_UVI_RESP_GAIN_UWCM2_PER_V 20.0f
#endif

#ifndef DOME_UVI_RESP_OFFSET_UWCM2
#define DOME_UVI_RESP_OFFSET_UWCM2 0.0f
#endif

#ifndef DOME_UVI_SUPPLY_MV
#define DOME_UVI_SUPPLY_MV 3300.0f
#endif

#ifndef DOME_UVI_ADC_OVERSAMPLE
#define DOME_UVI_ADC_OVERSAMPLE 16
#endif

#ifndef DOME_UVI_ADC_ATTEN
#define DOME_UVI_ADC_ATTEN ADC_ATTEN_DB_11
#endif

#ifndef DOME_UVI_ADC_BITWIDTH
#define DOME_UVI_ADC_BITWIDTH ADC_BITWIDTH_DEFAULT
#endif

#ifndef DOME_UVI_ADC_CHANNEL
#define DOME_UVI_ADC_CHANNEL ADC_CHANNEL_3
#endif

#ifndef DOME_UVI_SENSOR_MODE_ADC
#define DOME_UVI_SENSOR_MODE_ADC 0
#endif

#ifndef DOME_UVI_SENSOR_MODE_I2C
#define DOME_UVI_SENSOR_MODE_I2C 1
#endif

#if DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_I2C
#ifndef DOME_UVI_I2C_PORT
#define DOME_UVI_I2C_PORT I2C_NUM_0
#endif
#ifndef DOME_UVI_I2C_SDA
#define DOME_UVI_I2C_SDA DOME_I2C_SDA
#endif
#ifndef DOME_UVI_I2C_SCL
#define DOME_UVI_I2C_SCL DOME_I2C_SCL
#endif
#ifndef DOME_UVI_I2C_ADDR
#define DOME_UVI_I2C_ADDR 0x10
#endif
#ifndef DOME_UVI_I2C_REG_RESULT
#define DOME_UVI_I2C_REG_RESULT 0x00
#endif
#ifndef DOME_UVI_I2C_SCALE_UW_PER_LSB
#define DOME_UVI_I2C_SCALE_UW_PER_LSB 0.1f
#endif
#endif

#define ADC_FULL_SCALE 4095.0f
#define SAMPLE_PERIOD_US ((int64_t)DOME_UVI_SAMPLE_PERIOD_MS * 1000LL)
#define UVI_PER_UW_CM2 (1.0f / 2.5f)
#define SATURATION_MARGIN_RAW 8

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static bool s_initialized = false;
static bool s_have_measurement = false;
static uvi_sensor_measurement_t s_last = {0};
static int64_t s_last_sample_us = 0;

#if DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_I2C
static i2c_master_bus_handle_t s_uvi_bus = NULL;
static i2c_master_dev_handle_t s_uvi_dev = NULL;

static esp_err_t uvi_sensor_init_i2c_bus(void)
{
    if (s_uvi_bus && s_uvi_dev) {
        return ESP_OK;
    }
    if (!s_uvi_bus) {
        const i2c_master_bus_config_t bus_cfg = {
            .i2c_port = DOME_UVI_I2C_PORT,
            .sda_io_num = DOME_UVI_I2C_SDA,
            .scl_io_num = DOME_UVI_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = {
                .enable_internal_pullup = true,
            },
        };
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_uvi_bus);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (!s_uvi_dev) {
        const i2c_device_config_t dev_cfg = {
            .device_address = DOME_UVI_I2C_ADDR,
            .scl_speed_hz = 100000,
            .addr_bit_len = I2C_ADDR_BIT_LEN_7BIT,
        };
        esp_err_t err = i2c_master_bus_add_device(s_uvi_bus, &dev_cfg, &s_uvi_dev);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
#endif

static esp_err_t uvi_sensor_init_adc(void)
{
    if (s_adc) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = DOME_UVI_ADC_BITWIDTH,
        .atten = DOME_UVI_ADC_ATTEN,
    };
    err = adc_oneshot_config_channel(s_adc, DOME_UVI_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return err;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = DOME_UVI_ADC_ATTEN,
        .bitwidth = DOME_UVI_ADC_BITWIDTH,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "ADC calibration not supported, using raw to voltage");
        s_cali = NULL;
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_cali_create_scheme_curve_fitting failed: %s", esp_err_to_name(err));
        s_cali = NULL;
        return err;
    }

    return ESP_OK;
}

#if DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_ADC
static bool uvi_sensor_read_adc(float *voltage_v, int *raw_out)
{
    if (!voltage_v) {
        return false;
    }
    esp_err_t err = uvi_sensor_init_adc();
    if (err != ESP_OK) {
        return false;
    }

    int64_t raw_accum = 0;
    for (int i = 0; i < DOME_UVI_ADC_OVERSAMPLE; ++i) {
        int raw = 0;
        err = adc_oneshot_read(s_adc, DOME_UVI_ADC_CHANNEL, &raw);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
            return false;
        }
        raw_accum += raw;
    }
    float avg_raw = (float)raw_accum / (float)DOME_UVI_ADC_OVERSAMPLE;
    if (raw_out) {
        *raw_out = (int)lrintf(avg_raw);
    }

    if (s_cali) {
        int mv = 0;
        err = adc_cali_raw_to_voltage(s_cali, (int)lrintf(avg_raw), &mv);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adc_cali_raw_to_voltage failed: %s", esp_err_to_name(err));
            return false;
        }
        *voltage_v = (float)mv / 1000.0f;
    } else {
        *voltage_v = (avg_raw / ADC_FULL_SCALE) * (DOME_UVI_SUPPLY_MV / 1000.0f);
    }
    return true;
}
#endif

#if DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_I2C
static bool uvi_sensor_read_i2c(float *irradiance_uW_cm2)
{
    if (!irradiance_uW_cm2) {
        return false;
    }
    if (uvi_sensor_init_i2c_bus() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UVI I2C bus");
        return false;
    }
    uint8_t reg = DOME_UVI_I2C_REG_RESULT;
    uint8_t data[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_uvi_dev, &reg, 1, data, sizeof(data), pdMS_TO_TICKS(20));
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "UVI I2C read timeout");
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UVI I2C read failed: %s", esp_err_to_name(err));
        return false;
    }
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    *irradiance_uW_cm2 = (float)raw * DOME_UVI_I2C_SCALE_UW_PER_LSB;
    return true;
}
#endif

static void uvi_sensor_reset_measurement(void)
{
    memset(&s_last, 0, sizeof(s_last));
    s_have_measurement = false;
}

esp_err_t uvi_sensor_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
#if DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_ADC
    esp_err_t err = uvi_sensor_init_adc();
    if (err != ESP_OK) {
        return err;
    }
#elif DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_I2C
    esp_err_t err = uvi_sensor_init_i2c_bus();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C UVI sensor: %s", esp_err_to_name(err));
        return err;
    }
#else
#error "Unsupported DOME_UVI_SENSOR_MODE"
#endif
    s_initialized = true;
    uvi_sensor_reset_measurement();
    s_last_sample_us = esp_timer_get_time();
    return ESP_OK;
}

static void uvi_sensor_process_sample(float irradiance_uW_cm2, bool saturated)
{
    if (irradiance_uW_cm2 < 0.0f || !isfinite(irradiance_uW_cm2)) {
        irradiance_uW_cm2 = 0.0f;
    }

    float uvi = irradiance_uW_cm2 * UVI_PER_UW_CM2;
    if (!s_have_measurement) {
        s_last.irradiance_uW_cm2 = irradiance_uW_cm2;
        s_last.uvi = uvi;
        s_have_measurement = true;
    } else {
        s_last.irradiance_uW_cm2 += DOME_UVI_FILTER_ALPHA * (irradiance_uW_cm2 - s_last.irradiance_uW_cm2);
        s_last.uvi += DOME_UVI_FILTER_ALPHA * (uvi - s_last.uvi);
    }
    s_last.timestamp_us = esp_timer_get_time();
    s_last.valid = true;
    s_last.fault = false;
    s_last.saturated = saturated;
}

esp_err_t uvi_sensor_poll(void)
{
    if (!s_initialized) {
        esp_err_t err = uvi_sensor_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    int64_t now = esp_timer_get_time();
    if ((now - s_last_sample_us) < SAMPLE_PERIOD_US) {
        return ESP_OK;
    }
    s_last_sample_us = now;

    bool ok = false;
    bool saturated = false;
    float irradiance = 0.0f;
#if DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_ADC
    float voltage_v = 0.0f;
    int raw = 0;
    ok = uvi_sensor_read_adc(&voltage_v, &raw);
    if (ok) {
        irradiance = DOME_UVI_RESP_OFFSET_UWCM2 + (voltage_v * DOME_UVI_RESP_GAIN_UWCM2_PER_V);
        if (irradiance < 0.0f) {
            irradiance = 0.0f;
        }
        if (raw >= (int)(ADC_FULL_SCALE - SATURATION_MARGIN_RAW)) {
            saturated = true;
        }
    }
#elif DOME_UVI_SENSOR_MODE == DOME_UVI_SENSOR_MODE_I2C
    ok = uvi_sensor_read_i2c(&irradiance);
#endif

    if (ok) {
        uvi_sensor_process_sample(irradiance, saturated);
    } else {
        s_last.valid = false;
        s_last.fault = true;
    }
    return ok ? ESP_OK : ESP_FAIL;
}

bool uvi_sensor_get(uvi_sensor_measurement_t *out)
{
    if (!out) {
        return false;
    }
    if (!s_have_measurement) {
        return false;
    }
    *out = s_last;
    return true;
}

void uvi_sensor_force_reset(void)
{
    if (s_adc) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
    if (s_cali) {
        adc_cali_delete_scheme_curve_fitting(s_cali);
        s_cali = NULL;
    }
    s_initialized = false;
    uvi_sensor_reset_measurement();
}

