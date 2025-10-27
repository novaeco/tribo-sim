#include "calib.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>

static nvs_handle_t h;
static float g_k = 0.001f; // UVI per permille default (placeholder)
static float g_uvi_max = 1.0f;

esp_err_t calib_init(void){
    nvs_open("calib", NVS_READWRITE, &h);
    size_t sz=sizeof(float);
    nvs_get_blob(h, "uvb_k", &g_k, &sz);
    sz=sizeof(float);
    nvs_get_blob(h, "uvb_uvi_max", &g_uvi_max, &sz);
    return ESP_OK;
}

// Simple linear model: UVI = k * duty_pm
esp_err_t calib_set_uvb(float duty_pm, float uvi_meas){
    if (duty_pm <= 0) return ESP_ERR_INVALID_ARG;
    g_k = uvi_meas / duty_pm;
    nvs_set_blob(h,"uvb_k",&g_k,sizeof(g_k));
    nvs_commit(h);
    return ESP_OK;
}

esp_err_t calib_get_uvb(float* k_out, float* uvi_max_out){
    if (k_out) *k_out = g_k;
    if (uvi_max_out) *uvi_max_out = g_uvi_max;
    return ESP_OK;
}

esp_err_t calib_set_uvb_uvi_max(float uvi_max){
    g_uvi_max = uvi_max;
    nvs_set_blob(h,"uvb_uvi_max",&g_uvi_max,sizeof(g_uvi_max));
    nvs_commit(h);
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
