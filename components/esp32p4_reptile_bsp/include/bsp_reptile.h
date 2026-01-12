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

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================================
// HARDWARE PINOUT (NON-NEGOTIABLE)
// ====================================================================================

// Display
#define BSP_LCD_H_RES           1024
#define BSP_LCD_V_RES           600
#define BSP_LCD_BL_GPIO         26
#define BSP_LCD_RST_GPIO        27

// Touch (GT911)
#define BSP_TOUCH_I2C_SDA       22
#define BSP_TOUCH_I2C_SCL       23
#define BSP_TOUCH_I2C_INT       20
#define BSP_TOUCH_I2C_RST       21
#define BSP_TOUCH_I2C_FREQ_HZ   400000

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
