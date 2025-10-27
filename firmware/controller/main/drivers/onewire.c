#include "onewire.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
static const char* TAG="1WIRE";

static void ow_write_bit(int gpio, int v){
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(v ? 6 : 60);
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(v ? 64 : 10);
}
static int ow_read_bit(int gpio){
    int r;
    gpio_set_level(gpio, 0); esp_rom_delay_us(6);
    gpio_set_level(gpio, 1); esp_rom_delay_us(9);
    r = gpio_get_level(gpio);
    esp_rom_delay_us(55);
    return r;
}
static int ow_reset(int gpio){
    gpio_set_level(gpio, 0); esp_rom_delay_us(480);
    gpio_set_level(gpio, 1); esp_rom_delay_us(70);
    int p = gpio_get_level(gpio);
    esp_rom_delay_us(410);
    return !p; // presence = 0
}
static void ow_write_byte(int gpio, uint8_t v){
    for(int i=0;i<8;i++){ ow_write_bit(gpio, v&1); v>>=1; }
}
static uint8_t ow_read_byte(int gpio){
    uint8_t v=0;
    for(int i=0;i<8;i++){ v >>=1; if (ow_read_bit(gpio)) v |= 0x80; }
    return v;
}

esp_err_t ow_init(int gpio){
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
    gpio_set_level(gpio, 1);
    return ow_reset(gpio) ? ESP_OK : ESP_FAIL;
}

esp_err_t ow_read_ds18b20_celsius(int gpio, float* out_c){
    if (!ow_reset(gpio)) return ESP_FAIL;
    ow_write_byte(gpio, 0xCC); // SKIP ROM
    ow_write_byte(gpio, 0x44); // CONVERT T
    // wait max 750ms for 12-bit
    for (int i=0;i<80;i++){ esp_rom_delay_us(10000); } // 800ms
    if (!ow_reset(gpio)) return ESP_FAIL;
    ow_write_byte(gpio, 0xCC);
    ow_write_byte(gpio, 0xBE); // READ SCRATCHPAD
    uint8_t l = ow_read_byte(gpio);
    uint8_t h = ow_read_byte(gpio);
    int16_t raw = (h<<8) | l;
    *out_c = raw / 16.0f;
    return ESP_OK;
}
