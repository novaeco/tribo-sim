/**
 * @file bsp_display.c
 * @brief MIPI-DSI Display Driver for ESP32-P4 (JD9165BA/ST7701)
 * @version 2.0 - ESP-IDF 6.1 Compatible
 *
 * EXACT HARDWARE SPECIFICATIONS (DO NOT MODIFY):
 * - Resolution: 1024x600
 * - DSI Lanes: 2
 * - Bitrate: 800 Mbps per lane
 * - DPI Clock: 52 MHz
 * - Pixel Format: RGB565
 */

#include "bsp_reptile.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lvgl_port_disp.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"

static const char *TAG = "BSP_DISPLAY";

// ====================================================================================
// EXACT TIMING PARAMETERS (TESTED & WORKING)
// ====================================================================================

#define DSI_LANE_NUM            2
#define DSI_LANE_BITRATE_MBPS   800
#define DPI_CLOCK_MHZ           52
#define DSI_PHY_LDO_CHANNEL     3
#define DSI_PHY_VOLTAGE_MV      2500

static esp_lcd_panel_handle_t g_lcd_panel = NULL;
static esp_lcd_panel_handle_t g_lcd_ctrl_panel = NULL;

// ====================================================================================
// PUBLIC API IMPLEMENTATION
// ====================================================================================

esp_err_t bsp_display_init(lv_display_t **disp)
{
    ESP_LOGI(TAG, "Initializing 7-inch MIPI-DSI display (1024x600)");

    // Step 1: DSI PHY Power (LDO regulator)
    esp_ldo_channel_handle_t ldo_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = DSI_PHY_LDO_CHANNEL,
        .voltage_mv = DSI_PHY_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_chan));
    ESP_LOGI(TAG, "DSI PHY powered (LDO %dmV)", DSI_PHY_VOLTAGE_MV);

    // Step 2: DSI Bus Configuration
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus));
    ESP_LOGI(TAG, "DSI bus created (%d lanes @ %d Mbps)", DSI_LANE_NUM, DSI_LANE_BITRATE_MBPS);

    // Step 3: DPI Panel Configuration (ESP-IDF 6.1 API)
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLOCK_MHZ,
        .virtual_channel = 0,
        .num_fbs = 2, // Double buffering
        .video_timing = {
            .h_size = BSP_LCD_H_RES,
            .v_size = BSP_LCD_V_RES,
            .hsync_back_porch = 160,
            .hsync_pulse_width = 10,
            .hsync_front_porch = 160,
            .vsync_back_porch = 23,
            .vsync_pulse_width = 10,
            .vsync_front_porch = 12,
        },
    };

    // Step 4: Create DPI Panel
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(dsi_bus, &dpi_config, &g_lcd_panel));
    ESP_LOGI(TAG, "DPI panel created");

    // Step 5: Create ST7701 Controller Panel (for init commands)
    esp_lcd_panel_dev_config_t panel_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = BSP_LCD_RST_GPIO,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(dsi_bus, &panel_dev_config, &g_lcd_ctrl_panel));
    ESP_LOGI(TAG, "ST7701 controller panel created");

    // Step 6: Reset & Initialize ST7701 Controller
    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_lcd_ctrl_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_lcd_ctrl_panel));
    ESP_LOGI(TAG, "ST7701 controller initialized");

    // Step 7: Initialize DPI Panel
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_lcd_panel));
    ESP_LOGI(TAG, "DPI panel initialized");

    // Step 8: Backlight ON
    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = BIT64(BSP_LCD_BL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(BSP_LCD_BL_GPIO, 1); // Full brightness
    ESP_LOGI(TAG, "Backlight enabled");

    // Step 9: Create LVGL Display (if requested)
    if (disp) {
        const lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = NULL,
            .panel_handle = g_lcd_panel,
            .control_handle = g_lcd_ctrl_panel,
            .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
            .double_buffer = true,
            .trans_size = 0,
            .hres = BSP_LCD_H_RES,
            .vres = BSP_LCD_V_RES,
            .monochrome = false,
            .rotation = {
                .swap_xy = false,
                .mirror_x = false,
                .mirror_y = false,
            },
            .rounder_cb = NULL,
            .color_format = LV_COLOR_FORMAT_RGB565,
            .flags = {
                .buff_dma = false,
                .buff_spiram = true,
                .sw_rotate = false,
                .swap_bytes = false,
                .full_refresh = false,
                .direct_mode = false,
            },
        };
        const lvgl_port_display_dsi_cfg_t dsi_cfg = {
            .flags = {
                .avoid_tearing = true,
            },
        };

        *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
        if (*disp == NULL) {
            ESP_LOGE(TAG, "Failed to register LVGL display");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "LVGL display registered");
    }

    ESP_LOGI(TAG, "Display initialization complete");
    return ESP_OK;
}

void bsp_display_backlight_set(uint8_t brightness_percent)
{
    // Simple ON/OFF for now (PWM can be added later)
    gpio_set_level(BSP_LCD_BL_GPIO, brightness_percent > 0 ? 1 : 0);
}

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void)
{
    return g_lcd_panel;
}
