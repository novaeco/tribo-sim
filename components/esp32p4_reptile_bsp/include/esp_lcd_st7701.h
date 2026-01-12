/**
 * @file esp_lcd_st7701.h
 * @brief ST7701 LCD Panel Driver Header (MIPI-DSI)
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create ST7701 LCD panel
 *
 * @param[in] dsi_bus MIPI-DSI bus handle
 * @param[in] panel_dev_config Panel device configuration
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if parameter is invalid
 *      - ESP_ERR_NO_MEM if out of memory
 */
esp_err_t esp_lcd_new_panel_st7701(esp_lcd_dsi_bus_handle_t dsi_bus,
                                     const esp_lcd_panel_dev_config_t *panel_dev_config,
                                     esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
