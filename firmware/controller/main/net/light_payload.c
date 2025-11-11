#include "light_payload.h"

#include <stdio.h>
#include <string.h>

static void set_error(char *field_buf,
                      size_t field_buf_len,
                      const char *field,
                      char *detail_buf,
                      size_t detail_buf_len,
                      const char *detail)
{
    if (field_buf && field_buf_len > 0 && field) {
        snprintf(field_buf, field_buf_len, "%s", field);
    }
    if (detail_buf && detail_buf_len > 0 && detail) {
        snprintf(detail_buf, detail_buf_len, "%s", detail);
    }
}

static bool read_number_field(cJSON *parent,
                              const char *parent_path,
                              const char *field_name,
                              double *out,
                              char *field_buf,
                              size_t field_buf_len,
                              char *detail_buf,
                              size_t detail_buf_len)
{
    char field_path[48];
    if (snprintf(field_path, sizeof(field_path), "%s.%s", parent_path, field_name) >= (int)sizeof(field_path)) {
        field_path[sizeof(field_path) - 1] = '\0';
    }
    if (!cJSON_HasObjectItem(parent, field_name)) {
        set_error(field_buf, field_buf_len, field_path, detail_buf, detail_buf_len, "missing field");
        return false;
    }
    cJSON *item = cJSON_GetObjectItem(parent, field_name);
    if (!cJSON_IsNumber(item)) {
        set_error(field_buf, field_buf_len, field_path, detail_buf, detail_buf_len, "expected number");
        return false;
    }
    if (out) {
        *out = item->valuedouble;
    }
    return true;
}

esp_err_t light_payload_parse(cJSON *root,
                              light_payload_t *out,
                              char *field_buf,
                              size_t field_buf_len,
                              char *detail_buf,
                              size_t detail_buf_len)
{
    if (!out || !root) {
        return ESP_ERR_INVALID_ARG;
    }
    if (field_buf && field_buf_len > 0) {
        field_buf[0] = '\0';
    }
    if (detail_buf && detail_buf_len > 0) {
        detail_buf[0] = '\0';
    }
    if (!cJSON_IsObject(root)) {
        set_error(field_buf, field_buf_len, "root", detail_buf, detail_buf_len, "expected object");
        return ESP_ERR_INVALID_ARG;
    }

    if (!cJSON_HasObjectItem(root, "cct")) {
        set_error(field_buf, field_buf_len, "cct", detail_buf, detail_buf_len, "missing field");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_HasObjectItem(root, "uva")) {
        set_error(field_buf, field_buf_len, "uva", detail_buf, detail_buf_len, "missing field");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_HasObjectItem(root, "uvb")) {
        set_error(field_buf, field_buf_len, "uvb", detail_buf, detail_buf_len, "missing field");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *cct = cJSON_GetObjectItem(root, "cct");
    cJSON *uva = cJSON_GetObjectItem(root, "uva");
    cJSON *uvb = cJSON_GetObjectItem(root, "uvb");

    if (!cJSON_IsObject(cct)) {
        set_error(field_buf, field_buf_len, "cct", detail_buf, detail_buf_len, "expected object");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsObject(uva)) {
        set_error(field_buf, field_buf_len, "uva", detail_buf, detail_buf_len, "expected object");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsObject(uvb)) {
        set_error(field_buf, field_buf_len, "uvb", detail_buf, detail_buf_len, "expected object");
        return ESP_ERR_INVALID_ARG;
    }

    double value = 0.0;
    if (!read_number_field(cct, "cct", "day", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    out->cct_day = (uint16_t)((value < 0.0) ? 0 : value);

    if (!read_number_field(cct, "cct", "warm", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    out->cct_warm = (uint16_t)((value < 0.0) ? 0 : value);

    if (!read_number_field(uva, "uva", "set", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    out->uva_set = (uint16_t)((value < 0.0) ? 0 : value);

    if (!read_number_field(uva, "uva", "clamp", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    out->uva_clamp = (uint16_t)((value < 0.0) ? 0 : value);

    if (!read_number_field(uvb, "uvb", "set", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 10000.0f) {
        value = 10000.0f;
    }
    out->uvb_set = (float)value;

    if (!read_number_field(uvb, "uvb", "clamp", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 10000.0f) {
        value = 10000.0f;
    }
    out->uvb_clamp = (float)value;

    if (!read_number_field(uvb, "uvb", "period_s", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value < 1.0f) {
        value = 1.0f;
    }
    if (value > 255.0f) {
        value = 255.0f;
    }
    out->uvb_period = (uint8_t)value;

    if (!read_number_field(uvb, "uvb", "duty_pm", &value, field_buf, field_buf_len, detail_buf, detail_buf_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 10000.0f) {
        value = 10000.0f;
    }
    out->uvb_duty = (float)value;

    cJSON *sky = cJSON_GetObjectItem(root, "sky");
    if (sky) {
        if (!cJSON_IsNumber(sky)) {
            set_error(field_buf, field_buf_len, "sky", detail_buf, detail_buf_len, "expected number");
            return ESP_ERR_INVALID_ARG;
        }
        double sky_value = sky->valuedouble;
        if (sky_value < 0.0f) {
            sky_value = 0.0f;
        }
        if (sky_value > 255.0f) {
            sky_value = 255.0f;
        }
        out->has_sky = true;
        out->sky_value = (uint8_t)sky_value;
    } else {
        out->has_sky = false;
        out->sky_value = 0;
    }

    return ESP_OK;
}
