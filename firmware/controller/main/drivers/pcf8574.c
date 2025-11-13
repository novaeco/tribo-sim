#include "pcf8574.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"

static i2c_master_dev_handle_t s_pcf_dev = NULL;
static uint8_t s_pcf_addr = 0;

static esp_err_t ensure_pcf_device(uint8_t addr)
{
    if (s_pcf_dev && s_pcf_addr == addr) {
        return ESP_OK;
    }
    if (s_pcf_dev) {
        (void)i2c_master_bus_rm_device(s_pcf_dev);
        s_pcf_dev = NULL;
        s_pcf_addr = 0;
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
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_pcf_dev);
    if (err == ESP_OK) {
        s_pcf_addr = addr;
    }
    return err;
}

esp_err_t pcf8574_write(i2c_port_t port, uint8_t addr, uint8_t value)
{
    (void)port;
    esp_err_t err = ensure_pcf_device(addr);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(s_pcf_dev, &value, 1, pdMS_TO_TICKS(200));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
