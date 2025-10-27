#include "calib.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>

static nvs_handle_t s_calib_handle;
static nvs_handle_t *s_calib = NULL;
static float g_k = 0.001f; // UVI per permille default (placeholder)
static float g_uvi_max = 1.0f;

static void calib_close_handle(void)
{
    if (s_calib) {
        nvs_close(*s_calib);
        s_calib = NULL;
    }
}

void calib_deinit(void)
{
    calib_close_handle();
}

esp_err_t calib_init(void){
    if (s_calib) {
        return ESP_OK;
    }

    esp_err_t err = nvs_open("calib", NVS_READWRITE, &s_calib_handle);
    if (err != ESP_OK) {
        return err;
    }

    s_calib = &s_calib_handle;

    size_t sz = sizeof(float);
    err = nvs_get_blob(*s_calib, "uvb_k", &g_k, &sz);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        calib_close_handle();
        return err;
    }

    sz = sizeof(float);
    err = nvs_get_blob(*s_calib, "uvb_uvi_max", &g_uvi_max, &sz);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        calib_close_handle();
        return err;
    }

    return ESP_OK;
}

// Simple linear model: UVI = k * duty_pm
esp_err_t calib_set_uvb(float duty_pm, float uvi_meas){
    if (!s_calib) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duty_pm <= 0) return ESP_ERR_INVALID_ARG;
    g_k = uvi_meas / duty_pm;
    esp_err_t err = nvs_set_blob(*s_calib,"uvb_k",&g_k,sizeof(g_k));
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_commit(*s_calib);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t calib_get_uvb(float* k_out, float* uvi_max_out){
    if (!s_calib) {
        return ESP_ERR_INVALID_STATE;
    }
    if (k_out) *k_out = g_k;
    if (uvi_max_out) *uvi_max_out = g_uvi_max;
    return ESP_OK;
}

esp_err_t calib_set_uvb_uvi_max(float uvi_max){
    if (!s_calib) {
        return ESP_ERR_INVALID_STATE;
    }
    g_uvi_max = uvi_max;
    esp_err_t err = nvs_set_blob(*s_calib,"uvb_uvi_max",&g_uvi_max,sizeof(g_uvi_max));
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_commit(*s_calib);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

int uvb_duty_from_uvi(float uvi_target, float* duty_pm_out){
    if (g_k <= 0) return -1;
    float duty = uvi_target / g_k;
    if (duty < 0) duty = 0;
    if (duty > 10000) duty = 10000;
    if (duty_pm_out) *duty_pm_out = duty;
    return 0;
}
