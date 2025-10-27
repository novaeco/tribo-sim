#include "sensors.h"
#include "include/config.h"
#include "onewire.h"
#include "sht31.h"
#include "sht21.h"
#include "bme280.h"
#include "tca9548a.h"
#include "driver/i2c.h"
#include <string.h>

void sensors_init(void){}

int sensors_read(terra_sensors_t* out){
    memset(out, 0, sizeof(*out));
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_SENSORS);
#endif
    // DS18B20
    if (ow_init(CTRL_1W_BUS1)==ESP_OK){ out->t1_present=true; ow_read_ds18b20_celsius(CTRL_1W_BUS1, &out->t1_c); }
    if (ow_init(CTRL_1W_BUS2)==ESP_OK){ out->t2_present=true; ow_read_ds18b20_celsius(CTRL_1W_BUS2, &out->t2_c); }
    // SHT31 @ 0x44
    float t=0, rh=0;
    if (sht31_read(I2C_NUM_0, 0x44, &t, &rh)==ESP_OK){ out->sht31_present=true; out->sht31_t_c=t; out->sht31_rh=rh; }
    // SHT21 @ 0x40
    if (sht21_read(I2C_NUM_0, 0x40, &t, &rh)==ESP_OK){ out->sht21_present=true; out->sht21_t_c=t; out->sht21_rh=rh; }
    // BME280 @ 0x76
    bme280_data_t bd;
    if (bme280_init(I2C_NUM_0, 0x76)==ESP_OK && bme280_read(I2C_NUM_0, 0x76, &bd)==ESP_OK){
        out->bme_present=true;
        out->bme_t_c = bd.t_c; out->bme_rh = bd.rh; out->bme_p_hpa = bd.p_hpa;
    }
    return 0;
}
