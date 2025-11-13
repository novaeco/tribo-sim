#include "dome_i2c.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include <stdlib.h>
#include <string.h>

#define DOME_I2C_TIMEOUT_TICKS pdMS_TO_TICKS(1000)

static i2c_master_dev_handle_t s_dome_dev = NULL;
static uint8_t s_dome_addr = 0;

static esp_err_t ensure_dome_device(uint8_t addr)
{
    if (s_dome_dev && addr == s_dome_addr) {
        return ESP_OK;
    }

    if (s_dome_dev) {
        (void)i2c_master_bus_rm_device(s_dome_dev);
        s_dome_dev = NULL;
        s_dome_addr = 0;
    }

    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t dev_cfg = {
        .device_address = addr,
        .scl_speed_hz = i2c_bus_get_frequency_hz() ? i2c_bus_get_frequency_hz() : 400000,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7BIT,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dome_dev), "dome_i2c", "failed to add dome I2C device");
    s_dome_addr = addr;
    return ESP_OK;
}

esp_err_t dome_read_reg(i2c_port_t port, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    (void)port;
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_dome_device(addr);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit_receive(s_dome_dev, &reg, 1, data, len, DOME_I2C_TIMEOUT_TICKS);
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err == ESP_OK) {
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t dome_write_reg(i2c_port_t port, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{
    (void)port;
    esp_err_t err = ensure_dome_device(addr);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t buffer[1 + 32];
    uint8_t *tx = NULL;
    size_t tx_len = 0;

    if (len <= sizeof(buffer) - 1) {
        buffer[0] = reg;
        if (len && data) {
            memcpy(&buffer[1], data, len);
        }
        tx = buffer;
        tx_len = len + 1;
    } else {
        tx = malloc(len + 1);
        if (!tx) {
            return ESP_ERR_NO_MEM;
        }
        tx[0] = reg;
        if (len && data) {
            memcpy(&tx[1], data, len);
        }
        tx_len = len + 1;
    }

    err = i2c_master_transmit(s_dome_dev, tx, tx_len, DOME_I2C_TIMEOUT_TICKS);

    if (tx != buffer) {
        free(tx);
    }

    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err == ESP_OK) {
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}
