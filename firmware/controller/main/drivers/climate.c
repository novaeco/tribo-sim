#include "climate.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "include/config.h"

#define TAG "CLIMATE"

#define MIN_TEMP_C         5.0f
#define MAX_TEMP_C         45.0f
#define MIN_HUMIDITY_PCT   5.0f
#define MAX_HUMIDITY_PCT   100.0f
#define MIN_HYSTERESIS     0.1f
#define MAX_HYSTERESIS     10.0f
#define MIN_UVI            0.0f
#define MAX_UVI            20.0f
#define MINUTE_MAX         1440

static climate_schedule_t s_schedule = {0};
static climate_state_t    s_state = {0};
static SemaphoreHandle_t  s_measurement_mutex = NULL;
static climate_measurement_t s_measurement = {0};
static bool               s_measurement_valid = false;
static nvs_handle_t       s_nvs = 0;
static bool               s_initialized = false;
static int                s_temp_invalid_streak = 0;
static int                s_humidity_invalid_streak = 0;

static const climate_schedule_t k_default_schedule = {
    .day_start_minute = 8 * 60,
    .night_start_minute = 20 * 60,
    .day = {
        .temp_c = 32.0f,
        .humidity_pct = 55.0f,
        .temp_hysteresis_c = 1.5f,
        .humidity_hysteresis_pct = 6.0f,
    },
    .night = {
        .temp_c = 24.0f,
        .humidity_pct = 70.0f,
        .temp_hysteresis_c = 2.0f,
        .humidity_hysteresis_pct = 8.0f,
    },
    .day_uvi_max = 3.0f,
    .night_uvi_max = 0.4f,
};

static bool profile_active_for_minute(const climate_schedule_t *schedule, int minute, bool *is_day)
{
    if (!schedule || minute < 0) {
        return false;
    }
    minute %= MINUTE_MAX;
    if (minute < 0) {
        minute += MINUTE_MAX;
    }
    int day_start = schedule->day_start_minute % MINUTE_MAX;
    int night_start = schedule->night_start_minute % MINUTE_MAX;
    bool day_active = false;
    if (day_start == night_start) {
        day_active = true;
    } else if (day_start < night_start) {
        day_active = (minute >= day_start) && (minute < night_start);
    } else {
        day_active = (minute >= day_start) || (minute < night_start);
    }
    if (is_day) {
        *is_day = day_active;
    }
    return true;
}

static bool pick_temperature(const terra_sensors_t *sensors, float *value)
{
    if (!sensors || !value) {
        return false;
    }
    if (sensors->sht31_present) {
        *value = sensors->sht31_t_c;
        return true;
    }
    if (sensors->sht21_present) {
        *value = sensors->sht21_t_c;
        return true;
    }
    if (sensors->bme_present) {
        *value = sensors->bme_t_c;
        return true;
    }
    if (sensors->t1_present) {
        *value = sensors->t1_c;
        return true;
    }
    if (sensors->t2_present) {
        *value = sensors->t2_c;
        return true;
    }
    return false;
}

static bool pick_humidity(const terra_sensors_t *sensors, float *value)
{
    if (!sensors || !value) {
        return false;
    }
    if (sensors->sht31_present) {
        *value = sensors->sht31_rh;
        return true;
    }
    if (sensors->sht21_present) {
        *value = sensors->sht21_rh;
        return true;
    }
    if (sensors->bme_present) {
        *value = sensors->bme_rh;
        return true;
    }
    return false;
}

static esp_err_t climate_schedule_validate(const climate_schedule_t *schedule)
{
    if (!schedule) {
        return ESP_ERR_INVALID_ARG;
    }
    if (schedule->day_start_minute < 0 || schedule->day_start_minute >= MINUTE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (schedule->night_start_minute < 0 || schedule->night_start_minute >= MINUTE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    const climate_profile_t *profiles[2] = { &schedule->day, &schedule->night };
    for (size_t i = 0; i < 2; ++i) {
        const climate_profile_t *p = profiles[i];
        if (!p) {
            return ESP_ERR_INVALID_ARG;
        }
        if (p->temp_c < MIN_TEMP_C || p->temp_c > MAX_TEMP_C) {
            return ESP_ERR_INVALID_ARG;
        }
        if (p->humidity_pct < MIN_HUMIDITY_PCT || p->humidity_pct > MAX_HUMIDITY_PCT) {
            return ESP_ERR_INVALID_ARG;
        }
        if (p->temp_hysteresis_c < MIN_HYSTERESIS || p->temp_hysteresis_c > MAX_HYSTERESIS) {
            return ESP_ERR_INVALID_ARG;
        }
        if (p->humidity_hysteresis_pct < MIN_HYSTERESIS || p->humidity_hysteresis_pct > MAX_HYSTERESIS) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (schedule->day_uvi_max < MIN_UVI || schedule->day_uvi_max > MAX_UVI) {
        return ESP_ERR_INVALID_ARG;
    }
    if (schedule->night_uvi_max < MIN_UVI || schedule->night_uvi_max > MAX_UVI) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static cJSON *climate_schedule_to_json(const climate_schedule_t *schedule)
{
    if (!schedule) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "day_start_min", schedule->day_start_minute);
    cJSON_AddNumberToObject(root, "night_start_min", schedule->night_start_minute);
    cJSON *day = cJSON_AddObjectToObject(root, "day");
    cJSON *night = cJSON_AddObjectToObject(root, "night");
    if (!day || !night) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddNumberToObject(day, "temp_c", schedule->day.temp_c);
    cJSON_AddNumberToObject(day, "humidity_pct", schedule->day.humidity_pct);
    cJSON_AddNumberToObject(day, "temp_hysteresis_c", schedule->day.temp_hysteresis_c);
    cJSON_AddNumberToObject(day, "humidity_hysteresis_pct", schedule->day.humidity_hysteresis_pct);
    cJSON_AddNumberToObject(day, "uvi_max", schedule->day_uvi_max);
    cJSON_AddNumberToObject(night, "temp_c", schedule->night.temp_c);
    cJSON_AddNumberToObject(night, "humidity_pct", schedule->night.humidity_pct);
    cJSON_AddNumberToObject(night, "temp_hysteresis_c", schedule->night.temp_hysteresis_c);
    cJSON_AddNumberToObject(night, "humidity_hysteresis_pct", schedule->night.humidity_hysteresis_pct);
    cJSON_AddNumberToObject(night, "uvi_max", schedule->night_uvi_max);
    return root;
}

static esp_err_t climate_schedule_from_json(const char *json, climate_schedule_t *out)
{
    if (!json || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    cJSON *day = cJSON_GetObjectItemCaseSensitive(root, "day");
    cJSON *night = cJSON_GetObjectItemCaseSensitive(root, "night");
    if (!cJSON_IsNumber(cJSON_GetObjectItem(root, "day_start_min")) ||
        !cJSON_IsNumber(cJSON_GetObjectItem(root, "night_start_min")) ||
        !cJSON_IsObject(day) || !cJSON_IsObject(night)) {
        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    out->day_start_minute = cJSON_GetObjectItem(root, "day_start_min")->valueint;
    out->night_start_minute = cJSON_GetObjectItem(root, "night_start_min")->valueint;
    cJSON *f;
    f = cJSON_GetObjectItem(day, "temp_c");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->day.temp_c = f->valuedouble;
    f = cJSON_GetObjectItem(day, "humidity_pct");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->day.humidity_pct = f->valuedouble;
    f = cJSON_GetObjectItem(day, "temp_hysteresis_c");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->day.temp_hysteresis_c = f->valuedouble;
    f = cJSON_GetObjectItem(day, "humidity_hysteresis_pct");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->day.humidity_hysteresis_pct = f->valuedouble;
    f = cJSON_GetObjectItem(day, "uvi_max");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->day_uvi_max = f->valuedouble;

    f = cJSON_GetObjectItem(night, "temp_c");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->night.temp_c = f->valuedouble;
    f = cJSON_GetObjectItem(night, "humidity_pct");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->night.humidity_pct = f->valuedouble;
    f = cJSON_GetObjectItem(night, "temp_hysteresis_c");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->night.temp_hysteresis_c = f->valuedouble;
    f = cJSON_GetObjectItem(night, "humidity_hysteresis_pct");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->night.humidity_hysteresis_pct = f->valuedouble;
    f = cJSON_GetObjectItem(night, "uvi_max");
    if (!cJSON_IsNumber(f)) { result = ESP_ERR_INVALID_ARG; goto cleanup; }
    out->night_uvi_max = f->valuedouble;

    result = climate_schedule_validate(out);

cleanup:
    cJSON_Delete(root);
    return result;
}

static esp_err_t climate_schedule_store(const climate_schedule_t *schedule)
{
    if (!schedule || !s_nvs) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *json = climate_schedule_to_json(schedule);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = nvs_set_str(s_nvs, "schedule", text);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed storing schedule: %s", esp_err_to_name(err));
    }
    free(text);
    return err;
}

esp_err_t climate_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (!s_measurement_mutex) {
        s_measurement_mutex = xSemaphoreCreateMutex();
        if (!s_measurement_mutex) {
            ESP_LOGE(TAG, "Failed to create measurement mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = nvs_open("climate", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(climate) failed: %s", esp_err_to_name(err));
        return err;
    }
    size_t required = 0;
    err = nvs_get_str(s_nvs, "schedule", NULL, &required);
    if (err == ESP_OK && required > 1) {
        char *buffer = (char *)malloc(required);
        if (!buffer) {
            ESP_LOGE(TAG, "Failed to alloc %zu bytes for schedule", required);
            return ESP_ERR_NO_MEM;
        }
        err = nvs_get_str(s_nvs, "schedule", buffer, &required);
        if (err == ESP_OK) {
            err = climate_schedule_from_json(buffer, &s_schedule);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Stored schedule invalid, restoring defaults");
            }
        }
        free(buffer);
    }
    if (err != ESP_OK) {
        s_schedule = k_default_schedule;
        climate_schedule_store(&s_schedule);
        err = ESP_OK;
    }
    s_state.is_day = true;
    s_state.temp_setpoint_c = s_schedule.day.temp_c;
    s_state.humidity_setpoint_pct = s_schedule.day.humidity_pct;
    s_state.temp_hysteresis_c = s_schedule.day.temp_hysteresis_c;
    s_state.humidity_hysteresis_pct = s_schedule.day.humidity_hysteresis_pct;
    s_state.uvi_target = s_schedule.day_uvi_max;
    s_state.heater_on = false;
    s_state.lights_on = true;
    s_state.fan_pwm_percent = 0;
    s_state.temp_error_c = NAN;
    s_state.humidity_error_pct = NAN;
    s_initialized = true;
    return err;
}

esp_err_t climate_get_schedule(climate_schedule_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    *out = s_schedule;
    return ESP_OK;
}

esp_err_t climate_update_targets(const climate_schedule_t *schedule)
{
    if (!schedule) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = climate_schedule_validate(schedule);
    if (err != ESP_OK) {
        return err;
    }
    s_schedule = *schedule;
    err = climate_schedule_store(&s_schedule);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Climate schedule updated: day %d, night %d", schedule->day_start_minute, schedule->night_start_minute);
    }
    return err;
}

static uint8_t compute_fan_pwm(const climate_profile_t *profile, bool is_day, bool has_humidity, float humidity_value)
{
    uint8_t base = is_day ? 25 : 15;
    if (!has_humidity || !profile) {
        return base;
    }
    float upper = profile->humidity_pct + (profile->humidity_hysteresis_pct * 0.5f);
    float top = profile->humidity_pct + profile->humidity_hysteresis_pct;
    if (humidity_value > top) {
        return 100;
    }
    if (humidity_value > upper) {
        return 65;
    }
    float lower = profile->humidity_pct - profile->humidity_hysteresis_pct;
    if (humidity_value < lower) {
        return is_day ? 20 : 10;
    }
    return base;
}

void climate_tick(const terra_sensors_t *sensors, int minute_of_day, climate_state_t *out_state)
{
    if (!s_initialized) {
        return;
    }
    bool is_day = true;
    profile_active_for_minute(&s_schedule, minute_of_day, &is_day);
    const climate_profile_t *profile = is_day ? &s_schedule.day : &s_schedule.night;
    s_state.is_day = is_day;
    s_state.temp_setpoint_c = profile->temp_c;
    s_state.humidity_setpoint_pct = profile->humidity_pct;
    s_state.temp_hysteresis_c = profile->temp_hysteresis_c;
    s_state.humidity_hysteresis_pct = profile->humidity_hysteresis_pct;
    s_state.uvi_target = is_day ? s_schedule.day_uvi_max : s_schedule.night_uvi_max;
    s_state.lights_on = is_day;

    float temp_value = 0.0f;
    bool has_temp = pick_temperature(sensors, &temp_value);
    if (has_temp) {
        s_temp_invalid_streak = 0;
        float lower = profile->temp_c - (profile->temp_hysteresis_c * 0.5f);
        float upper = profile->temp_c + (profile->temp_hysteresis_c * 0.5f);
        if (!s_state.heater_on && temp_value < lower) {
            s_state.heater_on = true;
        } else if (s_state.heater_on && temp_value > upper) {
            s_state.heater_on = false;
        }
        s_state.temp_error_c = temp_value - profile->temp_c;
    } else {
        if (s_temp_invalid_streak < 10) {
            s_temp_invalid_streak++;
        }
        if (s_temp_invalid_streak >= 3) {
            s_state.heater_on = false;
        }
        s_state.temp_error_c = NAN;
    }

    float humidity_value = 0.0f;
    bool has_humidity = pick_humidity(sensors, &humidity_value);
    if (has_humidity) {
        s_humidity_invalid_streak = 0;
        s_state.humidity_error_pct = humidity_value - profile->humidity_pct;
    } else {
        if (s_humidity_invalid_streak < 10) {
            s_humidity_invalid_streak++;
        }
        s_state.humidity_error_pct = NAN;
    }
    s_state.fan_pwm_percent = compute_fan_pwm(profile, is_day, has_humidity, humidity_value);

    if (out_state) {
        *out_state = s_state;
    }
}

bool climate_get_state(climate_state_t *out_state)
{
    if (!out_state || !s_initialized) {
        return false;
    }
    *out_state = s_state;
    return true;
}

SemaphoreHandle_t climate_measurement_mutex(void)
{
    return s_measurement_mutex;
}

void climate_measurement_set_locked(const climate_measurement_t *m)
{
    if (!m) {
        return;
    }
    s_measurement = *m;
    s_measurement_valid = true;
}

bool climate_measurement_get(climate_measurement_t *out)
{
    if (!out || !s_measurement_mutex) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(s_measurement_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (s_measurement_valid) {
            *out = s_measurement;
            ok = true;
        }
        xSemaphoreGive(s_measurement_mutex);
    }
    return ok;
}

