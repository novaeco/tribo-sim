#include "bme280.h"

#include <math.h>

#include "esp_log.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "esp_check.h"

static uint8_t cal[26 + 16];
static int32_t t_fine;
static i2c_master_dev_handle_t s_bme_dev = NULL;
static uint8_t s_bme_addr = 0;

static esp_err_t ensure_bme_device(uint8_t addr)
{
    if (s_bme_dev && s_bme_addr == addr) {
        return ESP_OK;
    }
    if (s_bme_dev) {
        (void)i2c_master_bus_rm_device(s_bme_dev);
        s_bme_dev = NULL;
        s_bme_addr = 0;
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
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_bme_dev), "bme280", "failed to add BME280 device");
    s_bme_addr = addr;
    return ESP_OK;
}

static esp_err_t rd(uint8_t reg, uint8_t *d, size_t n)
{
    if (!s_bme_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_transmit_receive(s_bme_dev, &reg, 1, d, n, pdMS_TO_TICKS(200));
    if (err == ESP_OK || err == ESP_ERR_TIMEOUT) {
        return err;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t wr(uint8_t reg, uint8_t v)
{
    if (!s_bme_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t payload[2] = {reg, v};
    esp_err_t err = i2c_master_transmit(s_bme_dev, payload, sizeof(payload), pdMS_TO_TICKS(200));
    if (err == ESP_OK || err == ESP_ERR_TIMEOUT) {
        return err;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t bme280_init(i2c_port_t port, uint8_t addr)
{
    (void)port;
    esp_err_t err = ensure_bme_device(addr);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t id = 0;
    err = rd(0xD0, &id, 1);
    if (err != ESP_OK && err != ESP_ERR_INVALID_RESPONSE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(wr(0xE0, 0xB6), "bme280", "reset failed");
    vTaskDelay(pdMS_TO_TICKS(5)); // reset
    ESP_RETURN_ON_ERROR(rd(0x88, cal, 26), "bme280", "calibration block read failed");
    ESP_RETURN_ON_ERROR(rd(0xE1, cal + 26, 16), "bme280", "calibration block 2 read failed");
    ESP_RETURN_ON_ERROR(wr(0xF2, 0x01), "bme280", "humidity oversampling write failed");
    ESP_RETURN_ON_ERROR(wr(0xF4, 0x27), "bme280", "ctrl_meas write failed");
    ESP_RETURN_ON_ERROR(wr(0xF5, 0xA0), "bme280", "config write failed");
    return ESP_OK;
}

static inline uint16_t dig_T1(void) { return (uint16_t)cal[1] << 8 | cal[0]; }
static inline int16_t  dig_T2(void) { return (int16_t)((cal[3] << 8) | cal[2]); }
static inline int16_t  dig_T3(void) { return (int16_t)((cal[5] << 8) | cal[4]); }
static inline uint16_t dig_P1(void) { return (uint16_t)cal[7] << 8 | cal[6]; }
static inline int16_t  dig_P2(void) { return (int16_t)((cal[9] << 8) | cal[8]); }
static inline int16_t  dig_P3(void) { return (int16_t)((cal[11] << 8) | cal[10]); }
static inline int16_t  dig_P4(void) { return (int16_t)((cal[13] << 8) | cal[12]); }
static inline int16_t  dig_P5(void) { return (int16_t)((cal[15] << 8) | cal[14]); }
static inline int16_t  dig_P6(void) { return (int16_t)((cal[17] << 8) | cal[16]); }
static inline int16_t  dig_P7(void) { return (int16_t)((cal[19] << 8) | cal[18]); }
static inline int16_t  dig_P8(void) { return (int16_t)((cal[21] << 8) | cal[20]); }
static inline int16_t  dig_P9(void) { return (int16_t)((cal[23] << 8) | cal[22]); }
static inline uint8_t  dig_H1(void) { return cal[25]; }
static inline int16_t  dig_H2(void) { return (int16_t)((cal[27] << 8) | cal[26]); }
static inline uint8_t  dig_H3(void) { return cal[28]; }
static inline int16_t  dig_H4(void) { return (int16_t)(((int16_t)cal[29] << 4) | (cal[30] & 0x0F)); }
static inline int16_t  dig_H5(void) { return (int16_t)(((int16_t)cal[31] << 4) | (cal[30] >> 4)); }
static inline int8_t   dig_H6(void) { return (int8_t)cal[32]; }

float bme280_compensate_temperature(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1() << 1))) * (int32_t)dig_T2()) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)dig_T1()) * ((adc_T >> 4) - (int32_t)dig_T1())) >> 12) * (int32_t)dig_T3()) >> 14;
    t_fine = var1 + var2;
    int32_t t = (t_fine * 5 + 128) >> 8;
    return (float)t / 100.0f;
}

float bme280_compensate_pressure(int32_t adc_P)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dig_P6();
    var2 += ((var1 * (int64_t)dig_P5()) << 17);
    var2 += (((int64_t)dig_P4()) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3()) >> 8) + ((var1 * (int64_t)dig_P2()) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)dig_P1()) >> 33;
    if (var1 == 0) {
        return NAN;
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9()) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8()) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7()) << 4);
    return (float)p / 25600.0f;
}

float bme280_compensate_humidity(int32_t adc_H)
{
    int32_t v_x1_u32r = t_fine - ((int32_t)76800);
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)dig_H4()) << 20) - (((int32_t)dig_H5()) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * (int32_t)dig_H6()) >> 10) * (((v_x1_u32r * (int32_t)dig_H3()) >> 11) + ((int32_t)32768))) >> 10) +
                    ((int32_t)2097152)) * (int32_t)dig_H2() + 8192) >> 14));
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * (int32_t)dig_H1()) >> 4);
    if (v_x1_u32r < 0) {
        v_x1_u32r = 0;
    }
    if (v_x1_u32r > 419430400) {
        v_x1_u32r = 419430400;
    }
    return (float)(v_x1_u32r >> 12) / 1024.0f;
}

esp_err_t bme280_read(i2c_port_t port, uint8_t addr, bme280_data_t *out)
{
    (void)port;
    esp_err_t err = ensure_bme_device(addr);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t d[8];
    err = rd(0xF7, d, sizeof(d));
    if (err == ESP_ERR_TIMEOUT) {
        return err;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t adc_H = ((int32_t)d[6] << 8) | d[7];

    out->t_c = bme280_compensate_temperature(adc_T);
    out->p_hpa = bme280_compensate_pressure(adc_P);
    out->rh = bme280_compensate_humidity(adc_H);
    return ESP_OK;
}
