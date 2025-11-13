#include "ds3231.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"

static i2c_master_dev_handle_t s_ds3231_dev = NULL;
static uint8_t s_ds3231_addr = 0;

static uint8_t bcd2bin(uint8_t v){ return (v>>4)*10 + (v&0x0F); }

static esp_err_t ensure_ds3231_device(uint8_t addr)
{
    if (s_ds3231_dev && s_ds3231_addr == addr) {
        return ESP_OK;
    }
    if (s_ds3231_dev) {
        (void)i2c_master_bus_rm_device(s_ds3231_dev);
        s_ds3231_dev = NULL;
        s_ds3231_addr = 0;
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
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_ds3231_dev);
    if (err == ESP_OK) {
        s_ds3231_addr = addr;
    }
    return err;
}

esp_err_t ds3231_get_time(i2c_port_t port, uint8_t addr, ds3231_time_t* out){
    (void)port;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_ds3231_device(addr);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t reg = 0x00;
    uint8_t data[7] = {0};
    err = i2c_master_transmit_receive(s_ds3231_dev, &reg, 1, data, sizeof(data), pdMS_TO_TICKS(200));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->sec   = bcd2bin(data[0] & 0x7F);
    out->min   = bcd2bin(data[1] & 0x7F);
    out->hour  = bcd2bin(data[2] & 0x3F);
    out->day   = bcd2bin(data[4] & 0x3F);
    out->month = bcd2bin(data[5] & 0x1F);
    out->year  = 2000 + bcd2bin(data[6]);
    return ESP_OK;
}
