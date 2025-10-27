#include "httpd.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "drivers/dome_i2c.h"
#include "drivers/dome_bus.h"
#include "drivers/climate.h"
#include "drivers/sensors.h"
#include "drivers/calib.h"
#include "drivers/alarms.h"
#include "drivers/tca9548a.h"   // + si TCA_PRESENT
#include "include/config.h"
#include <stdio.h>              // + pour snprintf
#include <stdbool.h>
#include <math.h>

static const char* TAG="HTTPD";

static esp_err_t json_get_object(cJSON *parent, const char *key, cJSON **out)
{
    if (!parent || !cJSON_IsObject(parent)) {
        ESP_LOGW(TAG, "JSON parent invalid when looking for '%s'", key);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!item || !cJSON_IsObject(item)) {
        ESP_LOGW(TAG, "JSON key '%s' missing or not an object", key);
        return ESP_ERR_INVALID_ARG;
    }
    if (out) {
        *out = item;
    }
    return ESP_OK;
}

static esp_err_t json_get_int(cJSON *parent, const char *key, int min, int max, int *out)
{
    if (!parent || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!item) {
        ESP_LOGW(TAG, "JSON key '%s' missing", key);
        return ESP_ERR_NOT_FOUND;
    }
    if (!cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "JSON key '%s' must be numeric", key);
        return ESP_ERR_INVALID_ARG;
    }
    double value = item->valuedouble;
    double as_int = (double)item->valueint;
    if (fabs(value - as_int) > 1e-6) {
        ESP_LOGW(TAG, "JSON key '%s' must be an integer", key);
        return ESP_ERR_INVALID_ARG;
    }
    int ival = item->valueint;
    if (ival < min || ival > max) {
        ESP_LOGW(TAG, "JSON key '%s'=%d out of range [%d, %d]", key, ival, min, max);
        return ESP_ERR_INVALID_ARG;
    }
    *out = ival;
    return ESP_OK;
}

static esp_err_t json_get_double_optional(cJSON *parent, const char *key, double min, double max,
                                          bool *present, double *out)
{
    if (present) {
        *present = false;
    }
    if (!parent) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!item) {
        return ESP_OK;
    }
    if (present) {
        *present = true;
    }
    if (!cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "JSON key '%s' must be numeric", key);
        return ESP_ERR_INVALID_ARG;
    }
    double value = item->valuedouble;
    if (value < min || value > max) {
        ESP_LOGW(TAG, "JSON key '%s'=%f out of range [%f, %f]", key, value, min, max);
        return ESP_ERR_INVALID_ARG;
    }
    if (out) {
        *out = value;
    }
    return ESP_OK;
}

static esp_err_t json_get_double(cJSON *parent, const char *key, double min, double max, double *out)
{
    bool present = false;
    double value = 0.0;
    esp_err_t err = json_get_double_optional(parent, key, min, max, &present, &value);
    if (err != ESP_OK) {
        return err;
    }
    if (!present) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out) {
        *out = value;
    }
    return ESP_OK;
}

static esp_err_t climate_profile_from_json(cJSON *obj, climate_profile_t *profile, float *uvi_max)
{
    if (!obj || !profile || !uvi_max) {
        return ESP_ERR_INVALID_ARG;
    }
    double temp = 0.0;
    double hum = 0.0;
    double th = 0.0;
    double hh = 0.0;
    double uvi = 0.0;
    esp_err_t err;
    err = json_get_double(obj, "temp_c", CLIMATE_TEMP_MIN, CLIMATE_TEMP_MAX, &temp);
    if (err != ESP_OK) return err;
    err = json_get_double(obj, "humidity_pct", CLIMATE_HUM_MIN, CLIMATE_HUM_MAX, &hum);
    if (err != ESP_OK) return err;
    err = json_get_double(obj, "temp_hysteresis_c", CLIMATE_HYST_MIN, CLIMATE_HYST_MAX, &th);
    if (err != ESP_OK) return err;
    err = json_get_double(obj, "humidity_hysteresis_pct", CLIMATE_HYST_MIN, CLIMATE_HYST_MAX, &hh);
    if (err != ESP_OK) return err;
    err = json_get_double(obj, "uvi_max", CLIMATE_UVI_MIN, CLIMATE_UVI_MAX, &uvi);
    if (err != ESP_OK) return err;
    profile->temp_c = (float)temp;
    profile->humidity_pct = (float)hum;
    profile->temp_hysteresis_c = (float)th;
    profile->humidity_hysteresis_pct = (float)hh;
    *uvi_max = (float)uvi;
    return ESP_OK;
}

static void climate_profile_to_json(cJSON *parent, const char *name, const climate_profile_t *profile, float uvi_max)
{
    if (!parent || !profile || !name) {
        return;
    }
    cJSON *obj = cJSON_AddObjectToObject(parent, name);
    if (!obj) {
        return;
    }
    cJSON_AddNumberToObject(obj, "temp_c", profile->temp_c);
    cJSON_AddNumberToObject(obj, "humidity_pct", profile->humidity_pct);
    cJSON_AddNumberToObject(obj, "temp_hysteresis_c", profile->temp_hysteresis_c);
    cJSON_AddNumberToObject(obj, "humidity_hysteresis_pct", profile->humidity_hysteresis_pct);
    cJSON_AddNumberToObject(obj, "uvi_max", uvi_max);
}

static esp_err_t climate_schedule_from_json_obj(cJSON *root, climate_schedule_t *schedule)
{
    if (!root || !schedule) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *day = cJSON_GetObjectItemCaseSensitive(root, "day");
    cJSON *night = cJSON_GetObjectItemCaseSensitive(root, "night");
    if (!cJSON_IsObject(day) || !cJSON_IsObject(night)) {
        return ESP_ERR_INVALID_ARG;
    }
    int day_start = 0;
    int night_start = 0;
    esp_err_t err = json_get_int(root, "day_start_min", 0, 1439, &day_start);
    if (err != ESP_OK) return err;
    err = json_get_int(root, "night_start_min", 0, 1439, &night_start);
    if (err != ESP_OK) return err;
    err = climate_profile_from_json(day, &schedule->day, &schedule->day_uvi_max);
    if (err != ESP_OK) return err;
    err = climate_profile_from_json(night, &schedule->night, &schedule->night_uvi_max);
    if (err != ESP_OK) return err;
    schedule->day_start_minute = day_start;
    schedule->night_start_minute = night_start;
    return ESP_OK;
}

static void sensors_append_json(cJSON *parent, const terra_sensors_t *s)
{
    if (!parent || !s) {
        return;
    }
    cJSON_AddNumberToObject(parent,"ds18b20_bus1_c", s->t1_c);
    cJSON_AddBoolToObject(parent,"ds18b20_bus1_present", s->t1_present);
    cJSON_AddNumberToObject(parent,"ds18b20_bus2_c", s->t2_c);
    cJSON_AddBoolToObject(parent,"ds18b20_bus2_present", s->t2_present);
    cJSON_AddNumberToObject(parent,"sht31_t_c", s->sht31_t_c);
    cJSON_AddBoolToObject(parent,"sht31_present", s->sht31_present);
    cJSON_AddNumberToObject(parent,"sht31_rh", s->sht31_rh);
    cJSON_AddNumberToObject(parent,"sht21_t_c", s->sht21_t_c);
    cJSON_AddNumberToObject(parent,"sht21_rh", s->sht21_rh);
    cJSON_AddBoolToObject(parent,"sht21_present", s->sht21_present);
    cJSON_AddNumberToObject(parent,"bme280_t_c", s->bme_t_c);
    cJSON_AddNumberToObject(parent,"bme280_rh", s->bme_rh);
    cJSON_AddNumberToObject(parent,"bme280_p_hpa", s->bme_p_hpa);
    cJSON_AddBoolToObject(parent,"bme280_present", s->bme_present);
}

static esp_err_t climate_get_handler(httpd_req_t *req)
{
    climate_schedule_t schedule;
    esp_err_t err = climate_get_schedule(&schedule);
    if (err != ESP_OK) {
        send_json_error(req, "500 Internal Server Error", "failed to load schedule");
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        send_json_error(req, "500 Internal Server Error", "out of memory");
        return ESP_OK;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON *sched = cJSON_AddObjectToObject(root, "schedule");
    if (sched) {
        cJSON_AddNumberToObject(sched, "day_start_min", schedule.day_start_minute);
        cJSON_AddNumberToObject(sched, "night_start_min", schedule.night_start_minute);
        climate_profile_to_json(sched, "day", &schedule.day, schedule.day_uvi_max);
        climate_profile_to_json(sched, "night", &schedule.night, schedule.night_uvi_max);
    }
    climate_state_t state;
    if (climate_get_state(&state)) {
        cJSON *st = cJSON_AddObjectToObject(root, "state");
        if (st) {
            cJSON_AddBoolToObject(st, "is_day", state.is_day);
            cJSON_AddNumberToObject(st, "temp_setpoint_c", state.temp_setpoint_c);
            cJSON_AddNumberToObject(st, "humidity_setpoint_pct", state.humidity_setpoint_pct);
            cJSON_AddNumberToObject(st, "temp_hysteresis_c", state.temp_hysteresis_c);
            cJSON_AddNumberToObject(st, "humidity_hysteresis_pct", state.humidity_hysteresis_pct);
            cJSON_AddNumberToObject(st, "uvi_target", state.uvi_target);
            cJSON_AddBoolToObject(st, "heater_on", state.heater_on);
            cJSON_AddBoolToObject(st, "lights_on", state.lights_on);
            cJSON_AddNumberToObject(st, "fan_pwm_percent", state.fan_pwm_percent);
            if (isnan(state.temp_error_c)) {
                cJSON_AddNullToObject(st, "temp_error_c");
            } else {
                cJSON_AddNumberToObject(st, "temp_error_c", state.temp_error_c);
            }
            if (isnan(state.humidity_error_pct)) {
                cJSON_AddNullToObject(st, "humidity_error_pct");
            } else {
                cJSON_AddNumberToObject(st, "humidity_error_pct", state.humidity_error_pct);
            }
        }
    }
    climate_measurement_t meas;
    if (climate_measurement_get(&meas)) {
        cJSON *m = cJSON_AddObjectToObject(root, "measurement");
        if (m) {
            cJSON_AddNumberToObject(m, "timestamp_ms", (double)meas.timestamp_ms);
            if (isnan(meas.temp_drift_c)) {
                cJSON_AddNullToObject(m, "temp_drift_c");
            } else {
                cJSON_AddNumberToObject(m, "temp_drift_c", meas.temp_drift_c);
            }
            if (isnan(meas.humidity_drift_pct)) {
                cJSON_AddNullToObject(m, "humidity_drift_pct");
            } else {
                cJSON_AddNumberToObject(m, "humidity_drift_pct", meas.humidity_drift_pct);
            }
            cJSON *sens = cJSON_AddObjectToObject(m, "sensors");
            if (sens) {
                sensors_append_json(sens, &meas.sensors);
            }
        }
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        send_json_error(req, "500 Internal Server Error", "out of memory");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, payload);
    free(payload);
    return ESP_OK;
}

static esp_err_t climate_post_handler(httpd_req_t *req)
{
    char buf[512];
    esp_err_t err = httpd_read_body(req, buf, sizeof(buf), NULL);
    if (err != ESP_OK) {
        const char *msg = (err == ESP_ERR_INVALID_SIZE) ? "invalid payload size" : "failed to read request body";
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_OK;
    }
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json payload");
        return ESP_OK;
    }
    climate_schedule_t schedule;
    err = climate_schedule_from_json_obj(root, &schedule);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid schedule payload");
        return ESP_OK;
    }
    err = climate_update_targets(&schedule);
    if (err != ESP_OK) {
        send_json_error(req, "500 Internal Server Error", esp_err_to_name(err));
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t json_get_bool(cJSON *parent, const char *key, bool *out)
{
    if (!parent || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!item) {
        ESP_LOGW(TAG, "JSON key '%s' missing", key);
        return ESP_ERR_NOT_FOUND;
    }
    if (!cJSON_IsBool(item)) {
        ESP_LOGW(TAG, "JSON key '%s' must be boolean", key);
        return ESP_ERR_INVALID_ARG;
    }
    *out = cJSON_IsTrue(item);
    return ESP_OK;
}

static esp_err_t httpd_read_body(httpd_req_t *req, char *buf, size_t buf_sz, int *out_len)
{
    if (!req || !buf || buf_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t content_len = req->content_len;
    if (content_len == 0) {
        ESP_LOGW(TAG, "Empty payload on %s", req->uri);
        return ESP_ERR_INVALID_SIZE;
    }
    if (content_len >= buf_sz) {
        ESP_LOGW(TAG, "Payload too large on %s: %zu bytes (max %zu)", req->uri, content_len, buf_sz - 1);
        return ESP_ERR_INVALID_SIZE;
    }
    int total = 0;
    while ((size_t)total < content_len) {
        int ret = httpd_req_recv(req, buf + total, buf_sz - 1 - total);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGW(TAG, "httpd_req_recv failed on %s: %d", req->uri, ret);
            return ESP_FAIL;
        }
        total += ret;
    }
    buf[total] = '\0';
    if ((size_t)total != content_len) {
        ESP_LOGW(TAG, "Payload truncated on %s: expected %zu got %d", req->uri, content_len, total);
        return ESP_ERR_INVALID_SIZE;
    }
    if (out_len) {
        *out_len = total;
    }
    return ESP_OK;
}

#define PERMILLE_MIN 0
#define PERMILLE_MAX 10000
#define UVB_PERIOD_MIN 1
#define UVB_PERIOD_MAX 255
#define SKY_MIN 0
#define SKY_MAX 2
#define CALIB_DUTY_MIN 1.0
#define CALIB_DUTY_MAX 10000.0
#define CALIB_UVI_MIN 0.001
#define CALIB_UVI_MAX 100.0
#define CLIMATE_TEMP_MIN 5.0
#define CLIMATE_TEMP_MAX 45.0
#define CLIMATE_HUM_MIN 5.0
#define CLIMATE_HUM_MAX 100.0
#define CLIMATE_HYST_MIN 0.1
#define CLIMATE_HYST_MAX 10.0
#define CLIMATE_UVI_MIN 0.0
#define CLIMATE_UVI_MAX 20.0

static void send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"ok\":false,\"error\":\"%s\"}", message ? message : "unknown error");
    httpd_resp_sendstr(req, payload);
}

static esp_err_t root_get_handler(httpd_req_t *req){
    const char* html =
    "<!doctype html><html><head><meta charset='utf-8'><title>Terrarium S3</title>"
    "<style>body{font-family:system-ui;margin:24px;max-width:900px}"
    "label{display:block;margin-top:10px}input[type=range]{width:100%}"
    ".row{display:grid;grid-template-columns:220px 1fr;gap:12px;align-items:center}"
    "button{padding:8px 12px;margin-top:12px}</style></head><body>"
    "<h1>Terrarium S3 — Dôme</h1>"
    "<div id='status'></div>"
    "<div class='row'><label>Day (‰)</label><input id='day' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>Warm (‰)</label><input id='warm' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>UVA (‰)</label><input id='uva' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>UVB (‰) — UVI limited</label><input id='uvb' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>UVB period (s)</label><input id='uvb_per' type='number' min='5' max='600' value='60'></div>"
    "<div class='row'><label>UVB duty (‰)</label><input id='uvb_duty' type='number' min='0' max='10000' value='1000'></div>"
    "<div class='row'><label>Sky</label><select id='sky'><option value='0'>Off</option><option value='1'>Blue</option><option value='2'>Twinkle</option></select></div>"
    "<button onclick='save()'>Apply</button>"
    "<h2 style=margin-top:32px>Capteurs</h2><pre id='capteurs'></pre>"
    "<h2>Calibration UVB</h2>"
    "<div class='row'><label>UVI max (target)</label><input id='uvi_max' type='number' step='0.1' value='1.0'></div>"
    "<div class='row'><label>Dernier k</label><span id='kval'></span></div>"
    "<div class='row'><label>Mesure UVI @ duty (‰)</label><input id='cal_duty' type='number' min='0' max='10000' value='1000'></div>"
    "<div class='row'><label>UVI mesuré</label><input id='cal_uvi' type='number' step='0.01' value='0.1'></div>"
    "<button onclick='calib()'>Enregistrer calibration</button>"
    "<script>"
    "async function refresh(){"
    "  let r=await fetch('/api/light/dome0'); let j=await r.json();"
    "  day.value=j.cct.day; warm.value=j.cct.warm; uva.value=j.uva.set; uvb.value=j.uvb.set;"
    "  uvb_per.value=j.uvb.period_s; uvb_duty.value=j.uvb.duty_pm; sky.value=j.sky;"
    "  status.innerText='STATUS 0x'+j.status.toString(16);"
    "  let s=await (await fetch('/api/status')).json();"
    "  capteurs.textContent=JSON.stringify(s,null,2);"
    "  let kinfo=await (await fetch('/api/calibrate/uvb')).json();"
    "  uvi_max.value=kinfo.uvi_max; kval.textContent=kinfo.k.toFixed(6)+' UVI/‰';"
    "}"
    "async function save(){"
    " const p={cct:{day:+day.value,warm:+warm.value}, uva:{set:+uva.value},"
    " uvb:{set:+uvb.value,period_s:+uvb_per.value,duty_pm:+uvb_duty.value}, sky:+sky.value};"
    " await fetch('/api/light/dome0',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});"
    " refresh(); }"
    "async function calib(){"
    " const body={duty_pm:+cal_duty.value, uvi:+cal_uvi.value, uvi_max:+uvi_max.value};"
    " await fetch('/api/calibrate/uvb',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    " refresh(); }"
    "refresh();"
    "</script></body></html>";
    httpd_resp_send(req, html, -1);
    return ESP_OK;
}

static esp_err_t api_get_handler(httpd_req_t *req){
#if TCA_PRESENT
    dome_bus_select(TCA_CH_DOME0);
#endif
    uint8_t cct1_l=0,cct1_h=0,cct2_l=0,cct2_h=0, uva=0, uvb=0, sky=0, per=0, duty=0, status=0;
    dome_bus_read( 0x00, &status, 1);
    dome_bus_read( 0x02, &cct1_l, 1);
    dome_bus_read( 0x03, &cct1_h, 1);
    dome_bus_read( 0x04, &cct2_l, 1);
    dome_bus_read( 0x05, &cct2_h, 1);
    dome_bus_read( 0x06, &uva, 1);
    dome_bus_read( 0x07, &uvb, 1);
    dome_bus_read( 0x08, &sky, 1);
    dome_bus_read( 0x0B, &per, 1);
    dome_bus_read( 0x0C, &duty, 1);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j,"status",status);
    cJSON *cct = cJSON_AddObjectToObject(j,"cct");
    cJSON_AddNumberToObject(cct,"day", (int)(cct1_l | (cct1_h<<8)));
    cJSON_AddNumberToObject(cct,"warm",(int)(cct2_l | (cct2_h<<8)));
    cJSON *juva = cJSON_AddObjectToObject(j,"uva");
    cJSON_AddNumberToObject(juva,"set",uva*100);
    cJSON *juvb = cJSON_AddObjectToObject(j,"uvb");
    cJSON_AddNumberToObject(juvb,"set",uvb*100);
    cJSON_AddNumberToObject(juvb,"period_s", per);
    cJSON_AddNumberToObject(juvb,"duty_pm", duty);
    cJSON_AddNumberToObject(j,"sky", sky);
    httpd_resp_set_type(req, "application/json");
    char* out = cJSON_PrintUnformatted(j);
    httpd_resp_sendstr(req, out);
    free(out); cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t api_post_handler(httpd_req_t *req){
    char buf[512];
    esp_err_t err = httpd_read_body(req, buf, sizeof(buf), NULL);
    if (err != ESP_OK){
        const char *msg = (err == ESP_ERR_INVALID_SIZE) ? "invalid payload size" : "failed to read request body";
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_OK;
    }

    cJSON *j = cJSON_Parse(buf);
    if (!j){
        ESP_LOGW(TAG, "Invalid JSON payload on %s", req->uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json payload");
        return ESP_OK;
    }

    cJSON *cct = NULL;
    cJSON *uva = NULL;
    cJSON *uvb = NULL;
    err = json_get_object(j, "cct", &cct);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing object cct");
        goto cleanup;
    }
    err = json_get_object(j, "uva", &uva);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing object uva");
        goto cleanup;
    }
    err = json_get_object(j, "uvb", &uvb);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing object uvb");
        goto cleanup;
    }

    int day = 0;
    err = json_get_int(cct, "day", PERMILLE_MIN, PERMILLE_MAX, &day);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field cct.day");
        goto cleanup;
    }

    int warm = 0;
    err = json_get_int(cct, "warm", PERMILLE_MIN, PERMILLE_MAX, &warm);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field cct.warm");
        goto cleanup;
    }

    int uva_set = 0;
    err = json_get_int(uva, "set", PERMILLE_MIN, PERMILLE_MAX, &uva_set);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field uva.set");
        goto cleanup;
    }

    int uvb_set = 0;
    err = json_get_int(uvb, "set", PERMILLE_MIN, PERMILLE_MAX, &uvb_set);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field uvb.set");
        goto cleanup;
    }

    int uvb_per = 0;
    err = json_get_int(uvb, "period_s", UVB_PERIOD_MIN, UVB_PERIOD_MAX, &uvb_per);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field uvb.period_s");
        goto cleanup;
    }

    int uvb_duty = 0;
    err = json_get_int(uvb, "duty_pm", PERMILLE_MIN, PERMILLE_MAX, &uvb_duty);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field uvb.duty_pm");
        goto cleanup;
    }

    int sky_val = 0;
    err = json_get_int(j, "sky", SKY_MIN, SKY_MAX, &sky_val);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field sky");
        goto cleanup;
    }

#if TCA_PRESENT
    dome_bus_select(TCA_CH_DOME0);
#endif
    // Write CCT1/2
    uint8_t p[4] = { day & 0xFF, (day>>8)&0xFF, warm & 0xFF, (warm>>8)&0xFF };
    dome_bus_write( 0x02, p, 4);

    // UVA set (permille)
    uint8_t uva_b = (uint8_t)(uva_set/100);
    dome_bus_write( 0x06, &uva_b, 1);

    // UVB with UVI clamp via calibration k
    float k=0, uvi_max=0;
    esp_err_t calib_err = calib_get_uvb(&k,&uvi_max);
    if (calib_err != ESP_OK){
        ESP_LOGW(TAG, "calib_get_uvb failed during POST: %s", esp_err_to_name(calib_err));
        k = 0;
        uvi_max = 0;
    }
    float allowed_duty_pm = (uvi_max>0 && k>0) ? (uvi_max / k) : uvb_set;
    if (uvb_set > (int)allowed_duty_pm) uvb_set = (int)allowed_duty_pm;
    uint8_t uvb_b = (uint8_t)(uvb_set/100);
    dome_bus_write( 0x07, &uvb_b, 1);
    // period + duty
    uint8_t per_b = (uint8_t)uvb_per, duty_b = (uint8_t)uvb_duty;
    dome_bus_write( 0x0B, &per_b, 1);
    dome_bus_write( 0x0C, &duty_b, 1);

    // Sky
    uint8_t sky_b = (uint8_t)sky_val;
    dome_bus_write( 0x08, &sky_b, 1);

    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;

cleanup:
    cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req){
    terra_sensors_t s = {0};
    climate_measurement_t meas;
    bool has_meas = climate_measurement_get(&meas);
    if (has_meas) {
        s = meas.sensors;
    } else {
        sensors_read(&s);
    }
#if TCA_PRESENT
    dome_bus_select(TCA_CH_DOME0);
#endif
    uint8_t status=0, theat=0;
    dome_bus_read( 0x00, &status, 1);
    dome_bus_read( 0x20, &theat, 1);

    climate_state_t state;
    bool has_state = climate_get_state(&state);

    cJSON *j = cJSON_CreateObject();
    cJSON *sens = cJSON_AddObjectToObject(j,"sensors");
    sensors_append_json(sens, &s);
    cJSON *cl = cJSON_AddObjectToObject(j, "climate");
    if (cl) {
        if (has_state) {
            cJSON_AddBoolToObject(cl, "is_day", state.is_day);
            cJSON_AddBoolToObject(cl, "heater_on", state.heater_on);
            cJSON_AddBoolToObject(cl, "lights_on", state.lights_on);
            cJSON_AddNumberToObject(cl, "fan_pwm_percent", state.fan_pwm_percent);
            if (!isnan(state.temp_error_c)) {
                cJSON_AddNumberToObject(cl, "temp_error_c", state.temp_error_c);
            } else {
                cJSON_AddNullToObject(cl, "temp_error_c");
            }
            if (!isnan(state.humidity_error_pct)) {
                cJSON_AddNumberToObject(cl, "humidity_error_pct", state.humidity_error_pct);
            } else {
                cJSON_AddNullToObject(cl, "humidity_error_pct");
            }
        }
        if (has_meas) {
            cJSON_AddNumberToObject(cl, "timestamp_ms", (double)meas.timestamp_ms);
            if (!isnan(meas.temp_drift_c)) {
                cJSON_AddNumberToObject(cl, "temp_drift_c", meas.temp_drift_c);
            } else {
                cJSON_AddNullToObject(cl, "temp_drift_c");
            }
            if (!isnan(meas.humidity_drift_pct)) {
                cJSON_AddNumberToObject(cl, "humidity_drift_pct", meas.humidity_drift_pct);
            } else {
                cJSON_AddNullToObject(cl, "humidity_drift_pct");
            }
        }
    }

    cJSON *d = cJSON_AddObjectToObject(j,"dome");
    cJSON_AddNumberToObject(d,"status", status);
    cJSON_AddBoolToObject(d,"interlock", (status & (1<<5))!=0);
    cJSON_AddBoolToObject(d,"therm_hard", (status & (1<<6))!=0);
    cJSON_AddBoolToObject(d,"ot_soft", (status & 1)!=0);
    cJSON_AddBoolToObject(d,"bus_loss_degraded", dome_bus_is_degraded());
    cJSON_AddNumberToObject(d,"t_heatsink_c", (int)theat);
    char* out = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out); cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t calib_get_handler(httpd_req_t *req){
    float k=0, uvi_max=0;
    esp_err_t err = calib_get_uvb(&k,&uvi_max);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "calib_get_uvb failed: %s", esp_err_to_name(err));
        send_json_error(req, "500 Internal Server Error", esp_err_to_name(err));
        return ESP_OK;
    }
    cJSON *j=cJSON_CreateObject();
    cJSON_AddNumberToObject(j,"k",k);
    cJSON_AddNumberToObject(j,"uvi_max",uvi_max);
    char* out=cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_sendstr(req,out);
    free(out); cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t calib_post_handler(httpd_req_t *req){
    char buf[256];
    esp_err_t err = httpd_read_body(req, buf, sizeof(buf), NULL);
    if (err != ESP_OK){
        const char *msg = (err == ESP_ERR_INVALID_SIZE) ? "invalid payload size" : "failed to read request body";
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_OK;
    }

    cJSON *j = cJSON_Parse(buf);
    if (!j){
        ESP_LOGW(TAG, "Invalid JSON payload on %s", req->uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json payload");
        return ESP_OK;
    }

    bool has_duty = false;
    bool has_uvi = false;
    bool has_uvi_max = false;
    double duty_pm = 0.0;
    double uvi = 0.0;
    double uvi_max = 0.0;

    err = json_get_double_optional(j, "duty_pm", CALIB_DUTY_MIN, CALIB_DUTY_MAX, &has_duty, &duty_pm);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field duty_pm");
        goto cleanup;
    }
    err = json_get_double_optional(j, "uvi", CALIB_UVI_MIN, CALIB_UVI_MAX, &has_uvi, &uvi);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field uvi");
        goto cleanup;
    }
    err = json_get_double_optional(j, "uvi_max", CALIB_UVI_MIN, CALIB_UVI_MAX, &has_uvi_max, &uvi_max);
    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field uvi_max");
        goto cleanup;
    }

    if ((has_duty && !has_uvi) || (!has_duty && has_uvi) || (!has_duty && !has_uvi_max)){
        ESP_LOGW(TAG, "Calibration payload rejected (duty=%d, uvi=%d, uvi_max=%d)", has_duty, has_uvi, has_uvi_max);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid calibration payload");
        goto cleanup;
    }

    esp_err_t op_err = ESP_OK;

    if (has_uvi_max){
        op_err = calib_set_uvb_uvi_max((float)uvi_max);
        if (op_err != ESP_OK){
            ESP_LOGE(TAG, "calib_set_uvb_uvi_max failed: %s", esp_err_to_name(op_err));
        }
    }

    if (op_err == ESP_OK && has_duty){
        op_err = calib_set_uvb((float)duty_pm, (float)uvi);
        if (op_err != ESP_OK){
            ESP_LOGE(TAG, "calib_set_uvb failed: %s", esp_err_to_name(op_err));
        }
    }

    cJSON_Delete(j);

    if (op_err != ESP_OK){
        send_json_error(req, "500 Internal Server Error", esp_err_to_name(op_err));
        return ESP_OK;
    }

    httpd_resp_set_type(req,"application/json");
    httpd_resp_sendstr(req,"{\"ok\":true}");
    return ESP_OK;

cleanup:
    cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t alarms_mute_get(httpd_req_t *req){
    bool m = alarms_get_mute();
    char buf[64]; snprintf(buf,sizeof(buf),"{\"muted\":%s}", m?"true":"false");
    httpd_resp_set_type(req,"application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}
static esp_err_t alarms_mute_post(httpd_req_t *req){
    char buf[128];
    esp_err_t err = httpd_read_body(req, buf, sizeof(buf), NULL);
    if (err != ESP_OK){
        const char *msg = (err == ESP_ERR_INVALID_SIZE) ? "invalid payload size" : "failed to read request body";
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_OK;
    }

    cJSON *j = cJSON_Parse(buf);
    if (!j){
        ESP_LOGW(TAG, "Invalid JSON payload on %s", req->uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json payload");
        return ESP_OK;
    }

    bool muted = false;
    err = json_get_bool(j, "muted", &muted);
    if (err != ESP_OK){
        cJSON_Delete(j);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field muted");
        return ESP_OK;
    }

    esp_err_t op_err = alarms_set_mute(muted);
    cJSON_Delete(j);

    httpd_resp_set_type(req,"application/json");
    if (op_err == ESP_OK){
        httpd_resp_sendstr(req,"{\"ok\":true}");
    } else {
        char resp[128];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(op_err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, resp);
    }
    return ESP_OK;
}

void httpd_start_basic(void){
    esp_err_t calib_err = calib_init();
    if (calib_err != ESP_OK){
        ESP_LOGE(TAG, "calib_init failed: %s", esp_err_to_name(calib_err));
    }
    sensors_init();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=root_get_handler, .user_ctx=NULL};
        httpd_uri_t apig = {.uri="/api/light/dome0", .method=HTTP_GET, .handler=api_get_handler, .user_ctx=NULL};
        httpd_uri_t apip = {.uri="/api/light/dome0", .method=HTTP_POST, .handler=api_post_handler, .user_ctx=NULL};
        httpd_uri_t st   = {.uri="/api/status", .method=HTTP_GET, .handler=status_handler, .user_ctx=NULL};
        httpd_uri_t clg  = {.uri="/api/climate", .method=HTTP_GET, .handler=climate_get_handler, .user_ctx=NULL};
        httpd_uri_t clp  = {.uri="/api/climate", .method=HTTP_POST, .handler=climate_post_handler, .user_ctx=NULL};
        httpd_uri_t cg   = {.uri="/api/calibrate/uvb", .method=HTTP_GET, .handler=calib_get_handler, .user_ctx=NULL};
        httpd_uri_t cp   = {.uri="/api/calibrate/uvb", .method=HTTP_POST, .handler=calib_post_handler, .user_ctx=NULL};
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &apig);
        httpd_register_uri_handler(server, &apip);
        httpd_register_uri_handler(server, &st);
        httpd_register_uri_handler(server, &clg);
        httpd_register_uri_handler(server, &clp);
        httpd_register_uri_handler(server, &cg);
        httpd_register_uri_handler(server, &cp);
        httpd_uri_t amg = {.uri="/api/alarms/mute", .method=HTTP_GET, .handler=alarms_mute_get, .user_ctx=NULL};
        httpd_uri_t amp = {.uri="/api/alarms/mute", .method=HTTP_POST, .handler=alarms_mute_post, .user_ctx=NULL};
        httpd_register_uri_handler(server, &amg);
        httpd_register_uri_handler(server, &amp);
        ESP_LOGI(TAG, "HTTP server started");
    }
}
