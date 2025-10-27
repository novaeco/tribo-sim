#include "dome_bus.h"
#include "drivers/dome_i2c.h"
#include "include/config.h"
#include "tca9548a.h"
#include "driver/i2c.h"

static int dome_i2c_errs = 0;
static int dome_i2c_okstreak = 0;
static bool dome_degraded = false;
static uint8_t s_active_mask = TCA_CH_DOME0;

esp_err_t dome_bus_select(uint8_t channel_mask)
{
#if TCA_PRESENT
    if (channel_mask == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = tca9548a_select(I2C_NUM_0, TCA_ADDR, channel_mask);
    if (err == ESP_OK) {
        s_active_mask = channel_mask;
    }
    return err;
#else
    (void)channel_mask;
    return ESP_OK;
#endif
}

esp_err_t dome_bus_read(uint8_t reg, uint8_t* b, size_t n){
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, s_active_mask);
#endif
    esp_err_t r = dome_read_reg(I2C_NUM_0, DOME_I2C_ADDR, reg, b, n);
    if (r==ESP_OK){ dome_i2c_errs = 0; dome_i2c_okstreak++; if (dome_i2c_okstreak >= 3) dome_degraded = false; }
    else { dome_i2c_okstreak = 0; if (++dome_i2c_errs > 5) dome_degraded = true; }
    return r;
}
esp_err_t dome_bus_write(uint8_t reg, const uint8_t* b, size_t n){
    if (dome_degraded) return ESP_FAIL;
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, s_active_mask);
#endif
    esp_err_t r = dome_write_reg(I2C_NUM_0, DOME_I2C_ADDR, reg, b, n);
    if (r==ESP_OK){ dome_i2c_errs = 0; dome_i2c_okstreak++; if (dome_i2c_okstreak >= 3) dome_degraded = false; }
    else { dome_i2c_okstreak = 0; if (++dome_i2c_errs > 5) dome_degraded = true; }
    return r;
}
bool dome_bus_is_degraded(void){ return dome_degraded; }
void dome_bus_clear_degraded(void){ dome_i2c_errs = 0; dome_i2c_okstreak = 0; dome_degraded = false; }
