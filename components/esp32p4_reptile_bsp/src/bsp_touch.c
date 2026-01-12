/**
 * @file bsp_touch.c
 * @brief GT911 Touch Controller Driver (I2C)
 */

#include "bsp_reptile.h"
#include "esp_log.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port_touch.h"
#include "driver/i2c_master.h"

static const char *TAG = "BSP_TOUCH";

esp_err_t bsp_touch_init(lv_indev_t **indev, lv_display_t *disp)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL display handle is NULL; call lvgl_port_init and bsp_display_init first");
        return ESP_ERR_INVALID_STATE;
    }

    // Step 1: I2C Bus Configuration
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BSP_TOUCH_I2C_SDA,
        .scl_io_num = BSP_TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));
    ESP_LOGI(TAG, "I2C bus created");

    // Step 2: Panel IO for Touch
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = BSP_TOUCH_I2C_FREQ_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    // Step 3: GT911 Touch Configuration
    esp_lcd_touch_handle_t touch_handle = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_TOUCH_I2C_RST,
        .int_gpio_num = BSP_TOUCH_I2C_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &touch_handle));
    ESP_LOGI(TAG, "GT911 touch initialized");

    // Step 4: Create LVGL Input Device (if requested)
    if (indev) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = touch_handle,
            .scale = {
                .x = 1.0f,
                .y = 1.0f,
            },
        };
        *indev = lvgl_port_add_touch(&touch_cfg);
        if (*indev == NULL) {
            ESP_LOGE(TAG, "Failed to register LVGL touch input");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "LVGL touch input registered");
    }

    return ESP_OK;
}
