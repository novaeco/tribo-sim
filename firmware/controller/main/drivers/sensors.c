#include "sensors.h"
#include "include/config.h"
#include "onewire.h"
#include "sht31.h"
#include "sht21.h"
#include "bme280.h"
#include "tca9548a.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define TAG "SENSORS"

#ifndef SENSOR_FILTER_DEFAULT_MODE
#define SENSOR_FILTER_DEFAULT_MODE SENSOR_FILTER_MODE_EMA
#endif

#ifndef SENSOR_FILTER_EMA_ALPHA
#define SENSOR_FILTER_EMA_ALPHA 0.25f
#endif

#if SENSOR_FILTER_DEFAULT_MODE == SENSOR_FILTER_MODE_EMA
#define SENSOR_FILTER_DEFAULT_ENUM TERRA_SENSOR_FILTER_EMA
#elif SENSOR_FILTER_DEFAULT_MODE == SENSOR_FILTER_MODE_MEDIAN
#define SENSOR_FILTER_DEFAULT_ENUM TERRA_SENSOR_FILTER_MEDIAN3
#else
#define SENSOR_FILTER_DEFAULT_ENUM TERRA_SENSOR_FILTER_NONE
#endif

const char * const terra_sensor_names[TERRA_SENSOR_COUNT] = {
    [TERRA_SENSOR_DS18B20_EXT1] = "ds18b20_ext1",
    [TERRA_SENSOR_DS18B20_EXT2] = "ds18b20_ext2",
    [TERRA_SENSOR_SHT31]        = "sht31",
    [TERRA_SENSOR_SHT21]        = "sht21",
    [TERRA_SENSOR_BME280]       = "bme280",
};

typedef struct {
    terra_sensor_status_t status[TERRA_SENSOR_COUNT];
    terra_sensor_filter_mode_t filter_mode;
    float filter_alpha;
    bool filter_temp_init;
    bool filter_hum_init;
    float temp_ema;
    float hum_ema;
    float temp_window[3];
    float hum_window[3];
    size_t temp_window_count;
    size_t hum_window_count;
    size_t temp_window_index;
    size_t hum_window_index;
    bool initialized;
    bool bme_configured;
    bool sht31_configured;
    bool sht21_configured;
    bool ds1_configured;
    bool ds2_configured;
} sensors_state_t;

static sensors_state_t s_state = {0};

static esp_err_t i2c_write_cmd16(i2c_port_t port, uint8_t addr, uint16_t cmd)
{
    uint8_t buf[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(c, buf, sizeof(buf), true);
    i2c_master_stop(c);
    esp_err_t r = i2c_master_cmd_begin(port, c, 200 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    return r;
}

static esp_err_t sht21_user_reg_read(i2c_port_t port, uint8_t addr, uint8_t *reg)
{
    if (!reg) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, 0xE7, true);
    i2c_master_stop(c);
    esp_err_t r = i2c_master_cmd_begin(port, c, 200 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    if (r != ESP_OK) {
        return r;
    }
    c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(c, reg, I2C_MASTER_NACK);
    i2c_master_stop(c);
    r = i2c_master_cmd_begin(port, c, 200 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    return r;
}

static esp_err_t sht21_user_reg_write(i2c_port_t port, uint8_t addr, uint8_t reg)
{
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, 0xE6, true);
    i2c_master_write_byte(c, reg, true);
    i2c_master_stop(c);
    esp_err_t r = i2c_master_cmd_begin(port, c, 200 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    return r;
}

static void update_status(terra_sensor_slot_t slot, bool present, bool error, esp_err_t last_err)
{
    terra_sensor_status_t *st = &s_state.status[slot];
    st->present = present;
    st->error = error;
    st->last_error = last_err;
    if (error) {
        return;
    }
    st->last_valid_timestamp_ms = esp_timer_get_time() / 1000;
}

static void configure_default_filter(void)
{
    sensors_configure_filter(SENSOR_FILTER_DEFAULT_ENUM, SENSOR_FILTER_EMA_ALPHA);
}

void sensors_configure_filter(terra_sensor_filter_mode_t mode, float ema_alpha)
{
    if (ema_alpha <= 0.0f || ema_alpha >= 1.0f || isnan(ema_alpha)) {
        ema_alpha = SENSOR_FILTER_EMA_ALPHA;
    }
    s_state.filter_mode = mode;
    s_state.filter_alpha = ema_alpha;
    s_state.filter_temp_init = false;
    s_state.filter_hum_init = false;
    s_state.temp_window_count = 0;
    s_state.hum_window_count = 0;
    s_state.temp_window_index = 0;
    s_state.hum_window_index = 0;
}

terra_sensor_filter_mode_t sensors_filter_mode(void)
{
    return s_state.filter_mode;
}

float sensors_filter_alpha(void)
{
    return s_state.filter_alpha;
}

static void configure_ds18b20(int gpio, terra_sensor_slot_t slot, bool *configured)
{
    esp_err_t err = ow_init(gpio);
    if (err == ESP_OK) {
        *configured = true;
        update_status(slot, true, false, ESP_OK);
    } else {
        *configured = false;
        update_status(slot, false, true, err);
        ESP_LOGW(TAG, "DS18B20 init failed on GPIO %d: %s", gpio, esp_err_to_name(err));
    }
}

static void configure_sht31(void)
{
    esp_err_t err = i2c_write_cmd16(I2C_NUM_0, 0x44, 0x30A2); // soft reset
    if (err == ESP_OK) {
        err = i2c_write_cmd16(I2C_NUM_0, 0x44, 0x3066); // heater disable
    }
    if (err == ESP_OK) {
        s_state.sht31_configured = true;
        update_status(TERRA_SENSOR_SHT31, true, false, ESP_OK);
    } else {
        s_state.sht31_configured = false;
        update_status(TERRA_SENSOR_SHT31, false, true, err);
        ESP_LOGW(TAG, "SHT31 init failed: %s", esp_err_to_name(err));
    }
}

static void configure_sht21(void)
{
    uint8_t reg = 0;
    esp_err_t err = sht21_user_reg_read(I2C_NUM_0, 0x40, &reg);
    if (err == ESP_OK) {
        reg &= ~(1 << 2); // heater disable
        reg &= ~0x81;     // resolution bits -> 14b temp / 12b RH
        err = sht21_user_reg_write(I2C_NUM_0, 0x40, reg);
    }
    if (err == ESP_OK) {
        s_state.sht21_configured = true;
        update_status(TERRA_SENSOR_SHT21, true, false, ESP_OK);
    } else {
        s_state.sht21_configured = false;
        update_status(TERRA_SENSOR_SHT21, false, true, err);
        ESP_LOGW(TAG, "SHT21 init failed: %s", esp_err_to_name(err));
    }
}

static void configure_bme280(void)
{
    esp_err_t err = bme280_init(I2C_NUM_0, 0x76);
    if (err == ESP_OK) {
        s_state.bme_configured = true;
        update_status(TERRA_SENSOR_BME280, true, false, ESP_OK);
    } else {
        s_state.bme_configured = false;
        update_status(TERRA_SENSOR_BME280, false, true, err);
        ESP_LOGW(TAG, "BME280 init failed: %s", esp_err_to_name(err));
    }
}

void sensors_init(void)
{
    if (s_state.initialized) {
        return;
    }
    memset(&s_state, 0, sizeof(s_state));
    configure_default_filter();
#if TCA_PRESENT
    esp_err_t err = tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_SENSORS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TCA9548A select failed during init: %s", esp_err_to_name(err));
    }
#endif
    configure_ds18b20(CTRL_1W_BUS1, TERRA_SENSOR_DS18B20_EXT1, &s_state.ds1_configured);
    configure_ds18b20(CTRL_1W_BUS2, TERRA_SENSOR_DS18B20_EXT2, &s_state.ds2_configured);
    configure_sht31();
    configure_sht21();
    configure_bme280();
    s_state.initialized = true;
}

static void median_insert(float *window, size_t *count, size_t *index, float sample)
{
    window[*index] = sample;
    *index = (*index + 1) % 3;
    if (*count < 3) {
        (*count)++;
    }
}

static bool median_compute(const float *window, size_t count, float *out)
{
    if (count == 0 || !out) {
        return false;
    }
    float tmp[3];
    memcpy(tmp, window, sizeof(tmp));
    if (count < 3) {
        float sum = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            sum += tmp[i];
        }
        *out = sum / (float)count;
        return true;
    }
    for (size_t i = 0; i < 2; ++i) {
        for (size_t j = i + 1; j < 3; ++j) {
            if (tmp[j] < tmp[i]) {
                float t = tmp[i];
                tmp[i] = tmp[j];
                tmp[j] = t;
            }
        }
    }
    *out = tmp[1];
    return true;
}

static void apply_filter(float sample, bool is_temp, float *filtered, bool *valid)
{
    if (!filtered || !valid) {
        return;
    }
    if (!isfinite(sample)) {
        return;
    }
    if (s_state.filter_mode == TERRA_SENSOR_FILTER_NONE) {
        *filtered = sample;
        *valid = true;
        return;
    }
    if (s_state.filter_mode == TERRA_SENSOR_FILTER_EMA) {
        if (is_temp) {
            if (!s_state.filter_temp_init) {
                s_state.temp_ema = sample;
                s_state.filter_temp_init = true;
            } else {
                s_state.temp_ema = s_state.filter_alpha * sample + (1.0f - s_state.filter_alpha) * s_state.temp_ema;
            }
            *filtered = s_state.temp_ema;
            *valid = true;
        } else {
            if (!s_state.filter_hum_init) {
                s_state.hum_ema = sample;
                s_state.filter_hum_init = true;
            } else {
                s_state.hum_ema = s_state.filter_alpha * sample + (1.0f - s_state.filter_alpha) * s_state.hum_ema;
            }
            *filtered = s_state.hum_ema;
            *valid = true;
        }
        return;
    }
    if (s_state.filter_mode == TERRA_SENSOR_FILTER_MEDIAN3) {
        if (is_temp) {
            median_insert(s_state.temp_window, &s_state.temp_window_count, &s_state.temp_window_index, sample);
            if (median_compute(s_state.temp_window, s_state.temp_window_count, filtered)) {
                *valid = true;
            }
        } else {
            median_insert(s_state.hum_window, &s_state.hum_window_count, &s_state.hum_window_index, sample);
            if (median_compute(s_state.hum_window, s_state.hum_window_count, filtered)) {
                *valid = true;
            }
        }
    }
}

static void copy_status_snapshot(terra_sensor_status_t *dst, size_t dst_len)
{
    if (!dst || dst_len < TERRA_SENSOR_COUNT) {
        return;
    }
    memcpy(dst, s_state.status, sizeof(s_state.status));
}

uint32_t sensors_read(terra_sensors_t* out)
{
    if (!out) {
        return UINT32_MAX;
    }
    if (!s_state.initialized) {
        sensors_init();
    }
    memset(out, 0, sizeof(*out));
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_SENSORS);
#endif
    uint32_t faults = 0;
    float sample = NAN;
    esp_err_t err = ESP_OK;

    if (s_state.ds1_configured || s_state.status[TERRA_SENSOR_DS18B20_EXT1].present) {
        err = ow_read_ds18b20_celsius(CTRL_1W_BUS1, &sample);
        if (err == ESP_OK && isfinite(sample)) {
            out->t1_present = true;
            out->t1_c = sample;
            update_status(TERRA_SENSOR_DS18B20_EXT1, true, false, ESP_OK);
        } else {
            faults |= TERRA_SENSOR_FAULT_T1;
            update_status(TERRA_SENSOR_DS18B20_EXT1, s_state.status[TERRA_SENSOR_DS18B20_EXT1].present, true, err);
        }
    } else {
        faults |= TERRA_SENSOR_FAULT_T1;
    }

    if (s_state.ds2_configured || s_state.status[TERRA_SENSOR_DS18B20_EXT2].present) {
        err = ow_read_ds18b20_celsius(CTRL_1W_BUS2, &sample);
        if (err == ESP_OK && isfinite(sample)) {
            out->t2_present = true;
            out->t2_c = sample;
            update_status(TERRA_SENSOR_DS18B20_EXT2, true, false, ESP_OK);
        } else {
            faults |= TERRA_SENSOR_FAULT_T2;
            update_status(TERRA_SENSOR_DS18B20_EXT2, s_state.status[TERRA_SENSOR_DS18B20_EXT2].present, true, err);
        }
    } else {
        faults |= TERRA_SENSOR_FAULT_T2;
    }

    float t = NAN, rh = NAN;
    if (s_state.sht31_configured || s_state.status[TERRA_SENSOR_SHT31].present) {
        err = sht31_read(I2C_NUM_0, 0x44, &t, &rh);
        if (err == ESP_OK && isfinite(t) && isfinite(rh)) {
            out->sht31_present = true;
            out->sht31_t_c = t;
            out->sht31_rh = rh;
            update_status(TERRA_SENSOR_SHT31, true, false, ESP_OK);
        } else {
            faults |= TERRA_SENSOR_FAULT_SHT31;
            update_status(TERRA_SENSOR_SHT31, s_state.status[TERRA_SENSOR_SHT31].present, true, err);
        }
    } else {
        faults |= TERRA_SENSOR_FAULT_SHT31;
    }

    if (s_state.sht21_configured || s_state.status[TERRA_SENSOR_SHT21].present) {
        err = sht21_read(I2C_NUM_0, 0x40, &t, &rh);
        if (err == ESP_OK && isfinite(t) && isfinite(rh)) {
            out->sht21_present = true;
            out->sht21_t_c = t;
            out->sht21_rh = rh;
            update_status(TERRA_SENSOR_SHT21, true, false, ESP_OK);
        } else {
            faults |= TERRA_SENSOR_FAULT_SHT21;
            update_status(TERRA_SENSOR_SHT21, s_state.status[TERRA_SENSOR_SHT21].present, true, err);
        }
    } else {
        faults |= TERRA_SENSOR_FAULT_SHT21;
    }

    bme280_data_t bd = {0};
    if (s_state.bme_configured || s_state.status[TERRA_SENSOR_BME280].present) {
        err = bme280_read(I2C_NUM_0, 0x76, &bd);
        if (err == ESP_OK && isfinite(bd.t_c) && isfinite(bd.rh) && isfinite(bd.p_hpa)) {
            out->bme_present = true;
            out->bme_t_c = bd.t_c;
            out->bme_rh = bd.rh;
            out->bme_p_hpa = bd.p_hpa;
            update_status(TERRA_SENSOR_BME280, true, false, ESP_OK);
        } else {
            faults |= TERRA_SENSOR_FAULT_BME;
            update_status(TERRA_SENSOR_BME280, s_state.status[TERRA_SENSOR_BME280].present, true, err);
        }
    } else {
        faults |= TERRA_SENSOR_FAULT_BME;
    }

    float primary_temp = NAN;
    bool temp_valid = false;
    if (out->sht31_present) {
        primary_temp = out->sht31_t_c;
        temp_valid = true;
    } else if (out->sht21_present) {
        primary_temp = out->sht21_t_c;
        temp_valid = true;
    } else if (out->bme_present) {
        primary_temp = out->bme_t_c;
        temp_valid = true;
    } else if (out->t1_present) {
        primary_temp = out->t1_c;
        temp_valid = true;
    } else if (out->t2_present) {
        primary_temp = out->t2_c;
        temp_valid = true;
    }

    float primary_hum = NAN;
    bool hum_valid = false;
    if (out->sht31_present) {
        primary_hum = out->sht31_rh;
        hum_valid = true;
    } else if (out->sht21_present) {
        primary_hum = out->sht21_rh;
        hum_valid = true;
    } else if (out->bme_present) {
        primary_hum = out->bme_rh;
        hum_valid = true;
    }

    out->temp_filtered_c = NAN;
    out->humidity_filtered_pct = NAN;
    out->temp_filtered_valid = false;
    out->humidity_filtered_valid = false;
    if (temp_valid) {
        float filtered = NAN;
        bool valid = false;
        apply_filter(primary_temp, true, &filtered, &valid);
        if (valid) {
            out->temp_filtered_c = filtered;
            out->temp_filtered_valid = true;
        }
    }
    if (hum_valid) {
        float filtered = NAN;
        bool valid = false;
        apply_filter(primary_hum, false, &filtered, &valid);
        if (valid) {
            out->humidity_filtered_pct = filtered;
            out->humidity_filtered_valid = true;
        }
    }

    out->fault_mask = faults;
    copy_status_snapshot(out->status, TERRA_SENSOR_COUNT);
    return faults;
}
