#include "sht21.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// SHT21 (HTU21D) commands: Temp hold 0xE3, RH hold 0xE5, no-hold 0xF3/0xF5
static esp_err_t rd_cmd(i2c_port_t port, uint8_t addr, uint8_t cmd, uint8_t* data, size_t len){
    i2c_cmd_handle_t c=i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c,(addr<<1)|I2C_MASTER_WRITE,true);
    i2c_master_write_byte(c,cmd,true);
    i2c_master_stop(c);
    esp_err_t r=i2c_master_cmd_begin(port,c,200/portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    if (r!=ESP_OK) return r;
    // Wait conversion: max 85 ms; use 100 ms
    vTaskDelay(pdMS_TO_TICKS(100));
    c=i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c,(addr<<1)|I2C_MASTER_READ,true);
    if (len>1) i2c_master_read(c,data,len-1,I2C_MASTER_ACK);
    i2c_master_read_byte(c,&data[len-1],I2C_MASTER_NACK);
    i2c_master_stop(c);
    r=i2c_master_cmd_begin(port,c,200/portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    return r;
}
esp_err_t sht21_read(i2c_port_t port, uint8_t addr, float* t_c, float* rh){
    uint8_t t[3]={0}, h[3]={0};
    if (rd_cmd(port, addr, 0xF3, t, 3)!=ESP_OK) return ESP_FAIL;
    if (rd_cmd(port, addr, 0xF5, h, 3)!=ESP_OK) return ESP_FAIL;
    uint16_t tr = ((uint16_t)t[0]<<8) | (t[1] & 0xFC);
    uint16_t hr = ((uint16_t)h[0]<<8) | (h[1] & 0xFC);
    *t_c = -46.85f + 175.72f * (float)tr / 65536.0f;
    *rh  = -6.0f   + 125.0f  * (float)hr / 65536.0f;
    if (*rh<0) *rh=0; if (*rh>100) *rh=100;
    return ESP_OK;
}
