#include "tca9548a.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "esp_check.h"

static i2c_master_dev_handle_t s_tca_dev = NULL;
static uint8_t s_tca_addr = 0;

static esp_err_t ensure_tca_device(uint8_t addr)
{
    if (s_tca_dev && s_tca_addr == addr) {
        return ESP_OK;
    }
    if (s_tca_dev) {
        (void)i2c_master_bus_rm_device(s_tca_dev);
        s_tca_dev = NULL;
        s_tca_addr = 0;
    }
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t cfg = {
        .device_address = addr,
        .scl_speed_hz = i2c_bus_get_frequency_hz() ? i2c_bus_get_frequency_hz() : 400000,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7BIT,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_tca_dev), "tca9548a", "failed to register TCA9548A");
    s_tca_addr = addr;
    return ESP_OK;
}

esp_err_t tca9548a_select(i2c_port_t port, uint8_t addr, uint8_t mask)
{
    (void)port;
    esp_err_t err = ensure_tca_device(addr);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit(s_tca_dev, &mask, 1, pdMS_TO_TICKS(1000));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err == ESP_OK) {
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}
