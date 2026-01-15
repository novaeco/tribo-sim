/**
 * @file bsp_reptile.h
 * @brief ESP32-P4 Reptile Sim - Board Support Package
 * @version 1.0.0
 *
 * Hardware Abstraction Layer for:
 * - 7-inch MIPI-DSI Display (1024x600, JD9165BA/ST7701)
 * - GT911 Capacitive Touch (I2C)
 * - SD Card (SDMMC)
 */

#ifndef BSP_REPTILE_H
#define BSP_REPTILE_H

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================================
// HARDWARE PINOUT (NON-NEGOTIABLE)
// ====================================================================================

// Display
#ifdef CONFIG_APP_LCD_H_RES
#define BSP_LCD_H_RES           CONFIG_APP_LCD_H_RES
#else
#define BSP_LCD_H_RES           1024
#endif

#ifdef CONFIG_APP_LCD_V_RES
#define BSP_LCD_V_RES           CONFIG_APP_LCD_V_RES
#else
#define BSP_LCD_V_RES           600
#endif

#define BSP_LCD_BL_GPIO         26
#define BSP_LCD_RST_GPIO        27

#ifdef CONFIG_APP_LCD_HSYNC
#define BSP_LCD_HSYNC_PW        CONFIG_APP_LCD_HSYNC
#else
#define BSP_LCD_HSYNC_PW        24
#endif

#ifdef CONFIG_APP_LCD_HBP
#define BSP_LCD_HBP             CONFIG_APP_LCD_HBP
#else
#define BSP_LCD_HBP             136
#endif

#ifdef CONFIG_APP_LCD_HFP
#define BSP_LCD_HFP             CONFIG_APP_LCD_HFP
#else
#define BSP_LCD_HFP             160
#endif

#ifdef CONFIG_APP_LCD_VSYNC
#define BSP_LCD_VSYNC_PW        CONFIG_APP_LCD_VSYNC
#else
#define BSP_LCD_VSYNC_PW        2
#endif

#ifdef CONFIG_APP_LCD_VBP
#define BSP_LCD_VBP             CONFIG_APP_LCD_VBP
#else
#define BSP_LCD_VBP             21
#endif

#ifdef CONFIG_APP_LCD_VFP
#define BSP_LCD_VFP             CONFIG_APP_LCD_VFP
#else
#define BSP_LCD_VFP             12
#endif

#ifdef CONFIG_APP_LCD_PCLK
#define BSP_LCD_PCLK_HZ         CONFIG_APP_LCD_PCLK
#else
#define BSP_LCD_PCLK_HZ         51200000
#endif

// Touch (GT911)
#ifdef CONFIG_APP_TOUCH_I2C_SDA
#define BSP_TOUCH_I2C_SDA       CONFIG_APP_TOUCH_I2C_SDA
#else
#define BSP_TOUCH_I2C_SDA       22
#endif

#ifdef CONFIG_APP_TOUCH_I2C_SCL
#define BSP_TOUCH_I2C_SCL       CONFIG_APP_TOUCH_I2C_SCL
#else
#define BSP_TOUCH_I2C_SCL       23
#endif

#ifdef CONFIG_APP_TOUCH_I2C_INT_GPIO
#define BSP_TOUCH_I2C_INT       CONFIG_APP_TOUCH_I2C_INT_GPIO
#else
#define BSP_TOUCH_I2C_INT       20
#endif

#ifdef CONFIG_APP_TOUCH_I2C_RST_GPIO
#define BSP_TOUCH_I2C_RST       CONFIG_APP_TOUCH_I2C_RST_GPIO
#else
#define BSP_TOUCH_I2C_RST       21
#endif

#define BSP_TOUCH_I2C_FREQ_HZ   400000

#ifdef CONFIG_APP_TOUCH_SWAP_XY
#define BSP_TOUCH_SWAP_XY       CONFIG_APP_TOUCH_SWAP_XY
#else
#define BSP_TOUCH_SWAP_XY       0
#endif

#ifdef CONFIG_APP_TOUCH_MIRROR_X
#define BSP_TOUCH_MIRROR_X      CONFIG_APP_TOUCH_MIRROR_X
#else
#define BSP_TOUCH_MIRROR_X      0
#endif

#ifdef CONFIG_APP_TOUCH_INVERT_Y
#define BSP_TOUCH_MIRROR_Y      CONFIG_APP_TOUCH_INVERT_Y
#else
#define BSP_TOUCH_MIRROR_Y      0
#endif

#ifdef CONFIG_APP_DSI_LANE_NUM
#define BSP_DSI_LANE_NUM        CONFIG_APP_DSI_LANE_NUM
#else
#define BSP_DSI_LANE_NUM        2
#endif

#ifdef CONFIG_APP_DSI_LANE_BITRATE_MBPS
#define BSP_DSI_LANE_BITRATE_MBPS CONFIG_APP_DSI_LANE_BITRATE_MBPS
#else
#define BSP_DSI_LANE_BITRATE_MBPS 800
#endif

// SD Card (SDMMC Slot 0)
#define BSP_SD_CMD_GPIO         44
#define BSP_SD_CLK_GPIO         43
#define BSP_SD_D0_GPIO          39
#define BSP_SD_D1_GPIO          40
#define BSP_SD_D2_GPIO          41
#define BSP_SD_D3_GPIO          42
#define BSP_SD_MOUNT_POINT      "/sdcard"

// ====================================================================================
// PUBLIC API
// ====================================================================================

/**
 * @brief Initialize MIPI-DSI display and create LVGL display object
 * @param[out] disp Pointer to store LVGL display handle
 * @return ESP_OK on success
 */
esp_err_t bsp_display_init(lv_display_t **disp);

/**
 * @brief Initialize GT911 touch controller and create LVGL input device
 * @param[out] indev Pointer to store LVGL input device handle
 * @param[in] disp LVGL display handle to attach touch input
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_init(lv_indev_t **indev, lv_display_t *disp);

/**
 * @brief Mount SD card via SDMMC interface
 * @return ESP_OK on success
 */
esp_err_t bsp_sdcard_mount(void);

/**
 * @brief Set display backlight brightness
 * @param brightness_percent 0-100%
 */
void bsp_display_backlight_set(uint8_t brightness_percent);

/**
 * @brief Get LCD panel handle for direct access
 * @return esp_lcd_panel_handle_t LCD panel handle
 */
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_REPTILE_H
