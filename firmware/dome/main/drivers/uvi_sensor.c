#include "uvi_sensor.h"

#include <math.h>
#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/i2c.h"

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
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return false;
    }
    uint8_t reg = DOME_UVI_I2C_REG_RESULT;
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DOME_UVI_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, &reg, 1, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DOME_UVI_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    uint8_t data[2] = {0};
    i2c_master_read(cmd, data, sizeof(data), I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(DOME_UVI_I2C_PORT, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin failed: %s", esp_err_to_name(err));
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
    // Assume I2C peripheral already configured elsewhere.
    esp_err_t err = ESP_OK;
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

