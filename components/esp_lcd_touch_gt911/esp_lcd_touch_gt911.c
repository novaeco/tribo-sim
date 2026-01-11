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

#include "esp_lcd_touch_gt911.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include <stdlib.h>

#define TAG "GT911_STUB"

// GT911 register definitions.  The chip uses 16‑bit little‑endian
// addresses for configuration and data.  See the Goodix GT911
// programming guide for more details.
#define GT911_REG_STATUS     0x814E
#define GT911_REG_FIRST_X_L  0x8150
#define GT911_REG_FIRST_X_H  0x8151
#define GT911_REG_FIRST_Y_L  0x8152
#define GT911_REG_FIRST_Y_H  0x8153
#define GT911_REG_CLEAR      0x814E

// Internal driver structure.  Casted to esp_lcd_touch_handle_t for
// opaque API.  Stores I2C configuration, screen size and last touch
// state.
typedef struct {
    i2c_port_t i2c_port;    // I2C port number (0 or 1)
    uint8_t i2c_addr;       // Device I2C address (0x14 or 0x5D)
    int rst_gpio_num;       // Reset GPIO (not used in stub)
    int int_gpio_num;       // Interrupt GPIO (not used in stub)
    uint16_t x_max;         // Maximum X coordinate
    uint16_t y_max;         // Maximum Y coordinate
    bool swap_xy;           // Swap X and Y axes
    bool mirror_x;          // Mirror X axis
    bool mirror_y;          // Mirror Y axis
    // Last sampled coordinates and state
    uint16_t cur_x;
    uint16_t cur_y;
    bool touched;
} gt911_dev_t;

// Write a single byte to a 16‑bit register on the GT911.  The
// register address is encoded little‑endian (low byte first).
static esp_err_t gt911_i2c_write_byte(gt911_dev_t *dev, uint16_t reg, uint8_t data)
{
    uint8_t buf[3];
    buf[0] = reg & 0xFF;
    buf[1] = (reg >> 8) & 0xFF;
    buf[2] = data;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, sizeof(buf), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Read a sequence of bytes starting at a 16‑bit register from the GT911.
// The register address is encoded little‑endian (low byte first).
static esp_err_t gt911_i2c_read(gt911_dev_t *dev, uint16_t reg, uint8_t *buf, size_t len)
{
    // Write register address
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    uint8_t addr_bytes[2];
    addr_bytes[0] = reg & 0xFF;
    addr_bytes[1] = (reg >> 8) & 0xFF;
    i2c_master_write(cmd, addr_bytes, 2, true);
    // Repeated start and read
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Public API: create new GT911 instance.  Allocates memory for
// internal device structure and sets default configuration.
esp_err_t esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io,
                                      const esp_lcd_touch_config_t *config,
                                      esp_lcd_touch_handle_t *ret_tp)
{
    if (!config || !ret_tp) {
        return ESP_ERR_INVALID_ARG;
    }
    gt911_dev_t *dev = calloc(1, sizeof(gt911_dev_t));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }
    // Default configuration: I2C port 0, use 0x14 (primary) address.
    dev->i2c_port = 0;
    dev->i2c_addr = 0x14;
    dev->rst_gpio_num = config->rst_gpio_num;
    dev->int_gpio_num = config->int_gpio_num;
    dev->x_max = config->x_max;
    dev->y_max = config->y_max;
    dev->swap_xy = config->flags.swap_xy;
    dev->mirror_x = config->flags.mirror_x;
    dev->mirror_y = config->flags.mirror_y;
    dev->cur_x = 0;
    dev->cur_y = 0;
    dev->touched = false;
    *ret_tp = (esp_lcd_touch_handle_t)dev;
    ESP_LOGI(TAG, "GT911 stub created: res=%ux%u", dev->x_max, dev->y_max);
    return ESP_OK;
}

// Public API: set the I2C port and/or address for an existing GT911
// instance.  Pass 0x00 for addr or -1 for port to leave that field
// unchanged.  This allows the caller to override the default
// configuration after probing the bus.
esp_err_t esp_lcd_touch_gt911_set_i2c_config(esp_lcd_touch_handle_t tp,
                                             i2c_port_t port,
                                             uint8_t addr)
{
    if (!tp) {
        return ESP_ERR_INVALID_ARG;
    }
    gt911_dev_t *dev = (gt911_dev_t *)tp;
    if (port >= 0) {
        dev->i2c_port = port;
    }
    if (addr != 0x00) {
        dev->i2c_addr = addr;
    }
    ESP_LOGI(TAG, "GT911 I2C config set: port=%d, addr=0x%02X", dev->i2c_port, dev->i2c_addr);
    return ESP_OK;
}

// Public API: poll the touch controller for new data.  Reads the
// status register to determine if a touch event has occurred and
// updates the internal state accordingly.  Clears the interrupt flag
// on the device after reading.
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp)
{
    if (!tp) {
        return ESP_ERR_INVALID_ARG;
    }
    gt911_dev_t *dev = (gt911_dev_t *)tp;
    uint8_t status = 0;
    if (gt911_i2c_read(dev, GT911_REG_STATUS, &status, 1) != ESP_OK) {
        // Bus error: assume no touch
        dev->touched = false;
        return ESP_OK;
    }
    // The lower 4 bits of the status register contain the number of
    // touch points (0 means no touch).  Only single touch is
    // supported here, so we ignore additional points.
    uint8_t touch_points = status & 0x0F;
    if (touch_points > 0 && touch_points <= 5) {
        // Read first touch point coordinates (4 bytes: xL,xH,yL,yH)
        uint8_t buf[4] = {0};
        if (gt911_i2c_read(dev, GT911_REG_FIRST_X_L, buf, sizeof(buf)) == ESP_OK) {
            uint16_t x = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
            uint16_t y = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
            // Apply coordinate transformations
            if (dev->swap_xy) {
                uint16_t tmp = x;
                x = y;
                y = tmp;
            }
            if (dev->mirror_x) {
                x = dev->x_max - x;
            }
            if (dev->mirror_y) {
                y = dev->y_max - y;
            }
            dev->cur_x = x;
            dev->cur_y = y;
            dev->touched = true;
        } else {
            dev->touched = false;
        }
        // Clear status to acknowledge the event
        gt911_i2c_write_byte(dev, GT911_REG_CLEAR, 0x00);
    } else {
        dev->touched = false;
    }
    return ESP_OK;
}

// Public API: return the last read coordinates.  If a touch event was
// detected by esp_lcd_touch_read_data(), return true and set x, y
// accordingly.  Otherwise return false.  Only single touch is
// supported; strength and touch_num parameters may be NULL.
bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp,
                                   uint16_t *x,
                                   uint16_t *y,
                                   uint8_t *strength,
                                   uint8_t *touch_num,
                                   uint16_t max_points)
{
    if (!tp) {
        return false;
    }
    gt911_dev_t *dev = (gt911_dev_t *)tp;
    if (dev->touched) {
        if (x) {
            *x = dev->cur_x;
        }
        if (y) {
            *y = dev->cur_y;
        }
        if (strength) {
            *strength = 0;
        }
        if (touch_num) {
            *touch_num = 1;
        }
        // Mark the touch as consumed; subsequent calls return false
        dev->touched = false;
        return true;
    } else {
        if (touch_num) {
            *touch_num = 0;
        }
        return false;
    }
}