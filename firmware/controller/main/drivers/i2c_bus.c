#include "i2c_bus.h"
#include "esp_log.h"
esp_err_t i2c_bus_init(i2c_port_t port, int sda, int scl, uint32_t freq) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq
    };
    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    return i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
}
