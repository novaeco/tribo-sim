// SPDX-License-Identifier: Apache-2.0
//
// Minimal GT911 touch controller driver for use with ESP-IDF projects.
//
// This stub implements a subset of the esp_lcd_touch_gt911 API so that
// applications can interface with the Goodix GT911 touch controller
// without requiring the external `esp_lcd_touch_gt911` component from
// the Espressif component registry.  It provides basic I2C
// initialisation, polling and coordinate retrieval.  For simplicity
// and portability, the driver assumes the GT911 is connected to an
// I2C bus configured elsewhere in the application.

#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

// Create a new GT911 touch device instance.  This function allocates
// internal storage for the driver and returns a handle via `ret_tp`.
// The `io` parameter is ignored in this stub implementation; the
// application must configure the I2C bus separately.  The values in
// `config` define the screen resolution and GPIO pins for reset and
// interrupt.  Returns ESP_OK on success.
esp_err_t esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io,
                                      const esp_lcd_touch_config_t *config,
                                      esp_lcd_touch_handle_t *ret_tp);

// Set the I2C port number and address for a GT911 device.  Call this
// immediately after esp_lcd_touch_new_i2c_gt911() if your board uses
// a non‑default port or if you have probed the address dynamically.
// Passing 0x00 as addr leaves the address unchanged.  Returns ESP_OK.
esp_err_t esp_lcd_touch_gt911_set_i2c_config(esp_lcd_touch_handle_t tp,
                                             i2c_port_t port,
                                             uint8_t addr);

#ifdef __cplusplus
}
#endif
