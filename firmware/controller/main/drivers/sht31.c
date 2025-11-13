#include "sht31.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"

static i2c_master_dev_handle_t s_sht31_dev = NULL;
static uint8_t s_sht31_addr = 0;

static esp_err_t ensure_sht31_device(uint8_t addr)
{
    if (s_sht31_dev && s_sht31_addr == addr) {
        return ESP_OK;
    }
    if (s_sht31_dev) {
        (void)i2c_master_bus_rm_device(s_sht31_dev);
        s_sht31_dev = NULL;
        s_sht31_addr = 0;
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
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_sht31_dev);
    if (err == ESP_OK) {
        s_sht31_addr = addr;
    }
    return err;
}

esp_err_t sht31_read(i2c_port_t port, uint8_t addr, float *t_c, float *rh)
{
    (void)port;
    if (!t_c || !rh) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_sht31_device(addr);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t cmd[2] = {0x24, 0x00};
    err = i2c_master_transmit(s_sht31_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(200));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t buf[6] = {0};
    err = i2c_master_receive(s_sht31_dev, buf, sizeof(buf), pdMS_TO_TICKS(200));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint16_t t = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t h = ((uint16_t)buf[3] << 8) | buf[4];
    *t_c = -45.0f + 175.0f * (float)t / 65535.0f;
    *rh = 100.0f * (float)h / 65535.0f;
    return ESP_OK;
}
