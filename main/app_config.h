/**
 * @file app_config.h
 * @brief Shared application configuration and constants
 * @version 1.0
 * @date 2026-01-08
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "lvgl.h"

// ====================================================================================
// HARDWARE CONFIGURATION - JC1060P470C (7-inch 1024x600 JD9165BA)
// ====================================================================================

// Display resolution (7-inch IPS)
#define LCD_H_RES 1024
#define LCD_V_RES 600

#define LCD_RST_GPIO 5
#define LCD_BL_GPIO 23

// Touch I2C
#define TOUCH_I2C_SDA 7
#define TOUCH_I2C_SCL 8
#define TOUCH_I2C_FREQ_HZ 400000

// MIPI-DSI Configuration for JD9165BA (2 lanes)
#define DSI_LANE_NUM 2
#define DSI_LANE_BITRATE 800 // Mbps for 1024x600@60Hz
#define DPI_CLOCK_MHZ 52     // ~51.2 MHz as per datasheet

#define DSI_PHY_LDO_CHANNEL 3
#define DSI_PHY_VOLTAGE_MV 2500

// SD Card (SDMMC Slot 0)
#define SD_CMD_GPIO 44
#define SD_CLK_GPIO 43
#define SD_D0_GPIO 39
#define SD_D1_GPIO 40
#define SD_D2_GPIO 41
#define SD_D3_GPIO 42
#define SD_MOUNT_POINT "/sdcard"

// ====================================================================================
// COLOR THEME - REPTILE MANAGER (Premium Jungle/Terrarium inspired)
// ====================================================================================

// Backgrounds - Deeper, richer colors
#define COLOR_BG_DARK lv_color_hex(0x0D1F0D)
#define COLOR_BG_CARD lv_color_hex(0x1A2F1A)
#define COLOR_BG_HEADER lv_color_hex(0x132613)
#define COLOR_BG_INPUT lv_color_hex(0x0F1E0F)

// Accent colors - Nature-inspired
#define COLOR_PRIMARY lv_color_hex(0x4CAF50)
#define COLOR_SECONDARY lv_color_hex(0x81C784)
#define COLOR_ACCENT lv_color_hex(0xE8B04B)
#define COLOR_GOLD lv_color_hex(0xF5AF19)

// Status colors
#define COLOR_SUCCESS lv_color_hex(0x66BB6A)
#define COLOR_WARNING lv_color_hex(0xFFB74D)
#define COLOR_DANGER lv_color_hex(0xEF5350)
#define COLOR_INFO lv_color_hex(0x42A5F5)

// Text colors
#define COLOR_TEXT lv_color_hex(0xF1F8E9)
#define COLOR_TEXT_DIM lv_color_hex(0xA5D6A7)
#define COLOR_TEXT_MUTED lv_color_hex(0x6B8E6B)

// UI elements
#define COLOR_BORDER lv_color_hex(0x43A047)
#define COLOR_CARD_SHADOW lv_color_hex(0x0A150A)

// Icon theme colors
#define COLOR_ICON_WIFI lv_color_hex(0x64B5F6)
#define COLOR_ICON_BT lv_color_hex(0x4FC3F7)
#define COLOR_ICON_SD lv_color_hex(0xFFD54F)
#define COLOR_ICON_BATTERY lv_color_hex(0xAED581)
#define COLOR_ICON_ALERT lv_color_hex(0xFF8A65)

// ====================================================================================
// WiFi Configuration
// ====================================================================================

#define WIFI_SCAN_MAX_AP 20
#define WIFI_SSID_DEFAULT ""
#define WIFI_PASS_DEFAULT ""

// NVS Keys for WiFi
#define NVS_WIFI_NAMESPACE "wifi_creds"
#define NVS_WIFI_SSID_KEY "saved_ssid"
#define NVS_WIFI_PASS_KEY "saved_pass"

// ====================================================================================
// Battery Configuration
// ====================================================================================

#define BATTERY_ENABLED 0   // Set to 1 if battery monitoring available
#define BATTERY_SIMULATED 1 // Simulate battery for demo

// ====================================================================================
// Navbar / UI Layout
// ====================================================================================

#define STATUS_BAR_HEIGHT 50
#define NAVBAR_HEIGHT 60

#endif // APP_CONFIG_H
