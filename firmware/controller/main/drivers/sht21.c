#include "sht21.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

static i2c_master_dev_handle_t s_sht21_dev = NULL;
static uint8_t s_sht21_addr = 0;

static esp_err_t ensure_sht21_device(uint8_t addr)
{
    if (s_sht21_dev && s_sht21_addr == addr) {
        return ESP_OK;
    }
    if (s_sht21_dev) {
        (void)i2c_master_bus_rm_device(s_sht21_dev);
        s_sht21_dev = NULL;
        s_sht21_addr = 0;
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
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_sht21_dev);
    if (err == ESP_OK) {
        s_sht21_addr = addr;
    }
    return err;
}

// SHT21 (HTU21D) commands: Temp hold 0xE3, RH hold 0xE5, no-hold 0xF3/0xF5
static esp_err_t rd_cmd(uint8_t cmd, uint8_t *data, size_t len)
{
    if (!s_sht21_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_transmit(s_sht21_dev, &cmd, 1, pdMS_TO_TICKS(200));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Wait conversion: max 85 ms; use 100 ms
    vTaskDelay(pdMS_TO_TICKS(100));

    err = i2c_master_receive(s_sht21_dev, data, len, pdMS_TO_TICKS(200));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t sht21_read(i2c_port_t port, uint8_t addr, float* t_c, float* rh){
    (void)port;
    if (!t_c || !rh) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_sht21_device(addr);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t t[3] = {0}, h[3] = {0};
    if (rd_cmd(0xF3, t, 3) != ESP_OK) return ESP_FAIL;
    if (rd_cmd(0xF5, h, 3) != ESP_OK) return ESP_FAIL;

    uint16_t tr = ((uint16_t)t[0] << 8) | (t[1] & 0xFC);
    uint16_t hr = ((uint16_t)h[0] << 8) | (h[1] & 0xFC);

    *t_c = -46.85f + 175.72f * (float)tr / 65536.0f;
    *rh  =  -6.0f  + 125.0f   * (float)hr / 65536.0f;

    // Clamp 0..100 %RH (corrige l’avertissement d’indentation)
    if (*rh < 0.0f)   { *rh = 0.0f;   }
    if (*rh > 100.0f) { *rh = 100.0f; }

    return ESP_OK;
}
