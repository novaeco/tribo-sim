#include "ds3231.h"
static uint8_t bcd2bin(uint8_t v){ return (v>>4)*10 + (v&0x0F); }
esp_err_t ds3231_get_time(i2c_port_t port, uint8_t addr, ds3231_time_t* out){
    uint8_t reg = 0x00;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr<<1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, &reg, 1, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr<<1) | I2C_MASTER_READ, true);
    uint8_t data[7] = {0};
    i2c_master_read(cmd, data, 6, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data[6], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, 1000/portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) return ret;
    out->sec   = bcd2bin(data[0] & 0x7F);
    out->min   = bcd2bin(data[1] & 0x7F);
    out->hour  = bcd2bin(data[2] & 0x3F);
    out->day   = bcd2bin(data[4] & 0x3F);
    out->month = bcd2bin(data[5] & 0x1F);
    out->year  = 2000 + bcd2bin(data[6]);
    return ESP_OK;
}
