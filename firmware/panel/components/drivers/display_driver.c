#include "display_driver.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_bit_defs.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_check.h"
#include "lvgl_port.h"

static const char *TAG = "display";

#define PIN_NUM_DE           42
#define PIN_NUM_HSYNC        39
#define PIN_NUM_VSYNC        41
#define PIN_NUM_PCLK         45
#define PIN_NUM_DATA0        15
#define PIN_NUM_DATA1        7
#define PIN_NUM_DATA2        6
#define PIN_NUM_DATA3        5
#define PIN_NUM_DATA4        4
#define PIN_NUM_DATA5        9
#define PIN_NUM_DATA6        46
#define PIN_NUM_DATA7        3
#define PIN_NUM_DATA8        8
#define PIN_NUM_DATA9        18
#define PIN_NUM_DATA10       17
#define PIN_NUM_DATA11       16
#define PIN_NUM_DATA12       14
#define PIN_NUM_DATA13       13
#define PIN_NUM_DATA14       12
#define PIN_NUM_DATA15       11
#define PIN_NUM_BACKLIGHT     2
#define PIN_NUM_DISP_EN      1

#define LCD_BIT_PER_PIXEL    16

static const esp_lcd_rgb_timing_t panel_timing = {
    .pclk_hz = 16500000,
    .h_res = PANEL_H_RES,
    .v_res = PANEL_V_RES,
    .hsync_pulse_width = 20,
    .hsync_back_porch = 160,
    .hsync_front_porch = 140,
    .vsync_pulse_width = 10,
    .vsync_back_porch = 23,
    .vsync_front_porch = 12,
    .flags = {
        .pclk_active_neg = true,
    },
};

static esp_err_t init_gpio(void)
{
    const gpio_config_t bk_config = {
        .pin_bit_mask = BIT64(PIN_NUM_BACKLIGHT) | BIT64(PIN_NUM_DISP_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bk_config), TAG, "Backlight GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_NUM_DISP_EN, 1), TAG, "Failed to enable display");
    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_NUM_BACKLIGHT, 0), TAG, "Failed to set backlight low");
    return ESP_OK;
}

esp_err_t display_driver_init(void)
{
    ESP_RETURN_ON_ERROR(init_gpio(), TAG, "GPIO init failed");

    const int data_pins[16] = {
        PIN_NUM_DATA0, PIN_NUM_DATA1, PIN_NUM_DATA2, PIN_NUM_DATA3,
        PIN_NUM_DATA4, PIN_NUM_DATA5, PIN_NUM_DATA6, PIN_NUM_DATA7,
        PIN_NUM_DATA8, PIN_NUM_DATA9, PIN_NUM_DATA10, PIN_NUM_DATA11,
        PIN_NUM_DATA12, PIN_NUM_DATA13, PIN_NUM_DATA14, PIN_NUM_DATA15,
    };

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .psram_trans_align = 64,
        .num_fbs = 2,
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = panel_timing,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .flags = {
            .fb_in_psram = true,
            .double_fb = true,
            .refresh_on_demand = false,
        },
        .hsync_gpio_num = PIN_NUM_HSYNC,
        .vsync_gpio_num = PIN_NUM_VSYNC,
        .de_gpio_num = PIN_NUM_DE,
        .pclk_gpio_num = PIN_NUM_PCLK,
        .data_gpio_nums = data_pins,
        .disp_gpio_num = PIN_NUM_DISP_EN,
    };

    esp_lcd_panel_handle_t panel_handle;
    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB panel (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Panel on failed");

    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_NUM_BACKLIGHT, 1), TAG, "Backlight on failed");

    err = lvgl_port_init(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL port (%s)", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}
