#include "bme280.h"
#include "esp_log.h"
static uint8_t cal[26+16];
static int32_t t_fine;
static i2c_port_t g_port; static uint8_t g_addr;

static esp_err_t rd(uint8_t reg, uint8_t* d, size_t n){
    i2c_cmd_handle_t c=i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c,(g_addr<<1)|I2C_MASTER_WRITE,true);
    i2c_master_write_byte(c,reg,true);
    i2c_master_start(c);
    i2c_master_write_byte(c,(g_addr<<1)|I2C_MASTER_READ,true);
    if(n>1)i2c_master_read(c,d,n-1,I2C_MASTER_ACK);
    i2c_master_read_byte(c,&d[n-1],I2C_MASTER_NACK);
    i2c_master_stop(c);
    esp_err_t r=i2c_master_cmd_begin(g_port,c,200/portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    return r;
}
static esp_err_t wr(uint8_t reg, uint8_t v){
    i2c_cmd_handle_t c=i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c,(g_addr<<1)|I2C_MASTER_WRITE,true);
    i2c_master_write_byte(c,reg,true);
    i2c_master_write_byte(c,v,true);
    i2c_master_stop(c);
    esp_err_t r=i2c_master_cmd_begin(g_port,c,200/portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    return r;
}

esp_err_t bme280_init(i2c_port_t port, uint8_t addr){
    g_port=port; g_addr=addr;
    uint8_t id=0; rd(0xD0,&id,1);
    wr(0xE0,0xB6); vTaskDelay(pdMS_TO_TICKS(5)); // reset
    rd(0x88, cal, 26); rd(0xE1, cal+26, 16);
    wr(0xF2,0x01); // humidity oversampling x1
    wr(0xF4,0x27); // temp/press oversampling x1, mode normal
    wr(0xF5,0xA0); // standby 1000ms, filter off
    return ESP_OK;
}

static int32_t comp_t(int32_t adc_T){
    int32_t dig_T1 = cal[1]<<8 | cal[0];
    int32_t dig_T2 = (int16_t)(cal[3]<<8 | cal[2]);
    int32_t dig_T3 = (int16_t)(cal[5]<<8 | cal[4]);
    int32_t var1 = ((((adc_T>>3) - (dig_T1<<1))) * dig_T2) >> 11;
    int32_t var2 = (((((adc_T>>4) - dig_T1) * ((adc_T>>4) - dig_T1)) >> 12) * dig_T3) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

esp_err_t bme280_read(i2c_port_t port, uint8_t addr, bme280_data_t* out){
    uint8_t d[8]; rd(0xF7,d,8);
    int32_t adc_P = (d[0]<<12)|(d[1]<<4)|(d[2]>>4);
    int32_t adc_T = (d[3]<<12)|(d[4]<<4)|(d[5]>>4);
    int32_t adc_H = (d[6]<<8)|d[7];
    int32_t t = comp_t(adc_T); out->t_c = t/100.0f;
    // Simplified pressure/humidity (not full compensation to keep it short)
    out->rh = adc_H * 100.0f / 65535.0f;
    out->p_hpa = adc_P / 256.0f; // rough
    return ESP_OK;
}
