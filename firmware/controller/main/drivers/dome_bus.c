#include "dome_bus.h"
#include "drivers/dome_i2c.h"
#include "include/config.h"
#include "tca9548a.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#ifndef CONFIG_DOME_BUS_MUTEX_TIMEOUT_MS
#define CONFIG_DOME_BUS_MUTEX_TIMEOUT_MS 50
#endif

static const char *TAG = "dome_bus";

static int dome_i2c_errs = 0;
static int dome_i2c_okstreak = 0;
static bool dome_degraded = false;
static uint8_t s_active_mask = TCA_CH_DOME0;
static SemaphoreHandle_t s_bus_mutex = NULL;
static StaticSemaphore_t s_bus_mutex_buffer;

static bool dome_bus_mutex_ensure(void)
{
    if (s_bus_mutex) {
        return true;
    }
    s_bus_mutex = xSemaphoreCreateMutexStatic(&s_bus_mutex_buffer);
    if (!s_bus_mutex) {
        ESP_LOGE(TAG, "failed to allocate dome bus mutex");
        return false;
    }
    return true;
}

static TickType_t dome_bus_lock_timeout_ticks(void)
{
#if CONFIG_DOME_BUS_MUTEX_TIMEOUT_MS <= 0
    return portMAX_DELAY;
#else
    return pdMS_TO_TICKS(CONFIG_DOME_BUS_MUTEX_TIMEOUT_MS);
#endif
}

esp_err_t dome_bus_lock(void)
{
    if (!dome_bus_mutex_ensure()) {
        return ESP_FAIL;
    }
    TickType_t timeout = dome_bus_lock_timeout_ticks();
    if (xSemaphoreTake(s_bus_mutex, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void dome_bus_unlock(void)
{
    if (s_bus_mutex) {
        xSemaphoreGive(s_bus_mutex);
    }
}

esp_err_t dome_bus_select(uint8_t channel_mask)
{
#if TCA_PRESENT
    if (channel_mask == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = dome_bus_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = tca9548a_select(I2C_NUM_0, TCA_ADDR, channel_mask);
    if (err == ESP_OK) {
        s_active_mask = channel_mask;
    }
    dome_bus_unlock();
    return err;
#else
    (void)channel_mask;
    return ESP_OK;
#endif
}

esp_err_t dome_bus_read(uint8_t reg, uint8_t* b, size_t n){
    esp_err_t r = dome_bus_lock();
    if (r != ESP_OK) {
        return r;
    }
#if TCA_PRESENT
    r = tca9548a_select(I2C_NUM_0, TCA_ADDR, s_active_mask);
    if (r != ESP_OK) {
        dome_bus_unlock();
        return r;
    }
#endif
    r = dome_read_reg(I2C_NUM_0, DOME_I2C_ADDR, reg, b, n);
    if (r==ESP_OK){ dome_i2c_errs = 0; dome_i2c_okstreak++; if (dome_i2c_okstreak >= 3) dome_degraded = false; }
    else { dome_i2c_okstreak = 0; if (++dome_i2c_errs > 5) dome_degraded = true; }
    dome_bus_unlock();
    return r;
}
esp_err_t dome_bus_write(uint8_t reg, const uint8_t* b, size_t n){
    if (dome_degraded) return ESP_FAIL;
    esp_err_t r = dome_bus_lock();
    if (r != ESP_OK) {
        return r;
    }
#if TCA_PRESENT
    r = tca9548a_select(I2C_NUM_0, TCA_ADDR, s_active_mask);
    if (r != ESP_OK) {
        dome_bus_unlock();
        return r;
    }
#endif
    r = dome_write_reg(I2C_NUM_0, DOME_I2C_ADDR, reg, b, n);
    if (r==ESP_OK){ dome_i2c_errs = 0; dome_i2c_okstreak++; if (dome_i2c_okstreak >= 3) dome_degraded = false; }
    else { dome_i2c_okstreak = 0; if (++dome_i2c_errs > 5) dome_degraded = true; }
    dome_bus_unlock();
    return r;
}
bool dome_bus_is_degraded(void){ return dome_degraded; }
void dome_bus_clear_degraded(void){ dome_i2c_errs = 0; dome_i2c_okstreak = 0; dome_degraded = false; }
