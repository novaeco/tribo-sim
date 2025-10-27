#include "sht31.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
esp_err_t sht31_read(i2c_port_t port, uint8_t addr, float* t_c, float* rh){
    // single shot high repeatability with clock stretching disabled
    uint8_t cmd[2]={0x24,0x00};
    i2c_cmd_handle_t c=i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c,(addr<<1)|I2C_MASTER_WRITE,true);
    i2c_master_write(c,cmd,2,true);
    i2c_master_stop(c);
    esp_err_t r=i2c_master_cmd_begin(port,c,200/portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    if (r!=ESP_OK) return r;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t buf[6]={0};
    c=i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c,(addr<<1)|I2C_MASTER_READ,true);
    i2c_master_read(c,buf,5,I2C_MASTER_ACK);
    i2c_master_read_byte(c,&buf[5],I2C_MASTER_NACK);
    i2c_master_stop(c);
    r=i2c_master_cmd_begin(port,c,200/portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    if (r!=ESP_OK) return r;
    uint16_t t=(buf[0]<<8)|buf[1];
    uint16_t h=(buf[3]<<8)|buf[4];
    *t_c = -45.0f + 175.0f * (float)t / 65535.0f;
    *rh  = 100.0f * (float)h / 65535.0f;
    return ESP_OK;
}
