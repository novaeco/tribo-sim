#include "i2c_slave_if.h"
#include "esp_log.h"
static const char* TAG="I2C_SLAVE";
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
    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    return i2c_driver_install(port, I2C_MODE_SLAVE, 256, 256, 0);
}
int i2c_slave_if_read(uint8_t* buf, size_t maxlen, TickType_t to){
    return i2c_slave_read_buffer(I2C_NUM_0, buf, maxlen, to);
}
int i2c_slave_if_write(const uint8_t* buf, size_t len, TickType_t to){
    return i2c_slave_write_buffer(I2C_NUM_0, (uint8_t*)buf, len, to);
}
