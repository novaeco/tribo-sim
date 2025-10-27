#include "i2c_slave_if.h"
#include "esp_log.h"

static const char* TAG="I2C_SLAVE";
static i2c_port_t s_port = I2C_NUM_MAX;

esp_err_t i2c_slave_if_init(i2c_port_t port, int sda, int scl, uint8_t addr){
    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = addr
    };
    esp_err_t err = i2c_param_config(port, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C slave: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(port, I2C_MODE_SLAVE, 256, 256, 0);
    if (err == ESP_OK) {
        s_port = port;
    } else {
        ESP_LOGE(TAG, "Failed to install I2C slave driver: %s", esp_err_to_name(err));
    }
    return err;
}
int i2c_slave_if_read(uint8_t* buf, size_t maxlen, TickType_t to){
    if (s_port >= I2C_NUM_MAX) {
        ESP_LOGE(TAG, "Attempted to read before initializing I2C slave interface");
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_slave_read_buffer(s_port, buf, maxlen, to);
}
int i2c_slave_if_write(const uint8_t* buf, size_t len, TickType_t to){
    if (s_port >= I2C_NUM_MAX) {
        ESP_LOGE(TAG, "Attempted to write before initializing I2C slave interface");
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_slave_write_buffer(s_port, (uint8_t*)buf, len, to);
}
esp_err_t i2c_slave_if_deinit(void){
    if (s_port >= I2C_NUM_MAX) {
        ESP_LOGW(TAG, "I2C slave interface already deinitialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_driver_delete(s_port);
    if (err == ESP_OK) {
        s_port = I2C_NUM_MAX;
    } else {
        ESP_LOGE(TAG, "Failed to delete I2C slave driver: %s", esp_err_to_name(err));
    }
    return err;
}
