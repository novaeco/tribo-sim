#include "i2c_bus.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";
static i2c_master_bus_handle_t s_bus = NULL;
static uint32_t s_bus_freq_hz = 0;

esp_err_t i2c_bus_init(i2c_port_t port, int sda, int scl, uint32_t freq)
{
    if (s_bus) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "failed to create I2C master bus");

    s_bus_freq_hz = freq;
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus;
}

uint32_t i2c_bus_get_frequency_hz(void)
{
    return s_bus_freq_hz;
}
