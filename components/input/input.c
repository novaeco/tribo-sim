// Implementation of the input subsystem for the reptile simulation.
//
// Handles initialisation of the GT911 touch controller via I2C and
// provides a polling task that reads touch data and updates the
// LVGL input driver state.  Updated for LVGL 9.x and ESP-IDF 6.x API.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"

#include "input.h"

static const char *TAG = "INPUT";

// Handle for the I2C bus and touch controller
static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static esp_lcd_panel_io_handle_t touch_io = NULL;

// LVGL input device
static lv_indev_t *touch_indev = NULL;

// Mutex to protect shared touch state
static SemaphoreHandle_t touch_mutex;

// Shared touch state accessed by LVGL via callback
static bool s_touch_pressed = false;
static int32_t s_touch_x = 0;
static int32_t s_touch_y = 0;

// LVGL input callback for LVGL 9.x API
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (xSemaphoreTake(touch_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        data->point.x = s_touch_x;
        data->point.y = s_touch_y;
        data->state = s_touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        xSemaphoreGive(touch_mutex);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void touch_init(void)
{
    ESP_LOGI(TAG, "Initializing touch controller GT911");

    // Configure and create the I2C master bus using new API
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = CONFIG_TOUCH_I2C_PORT,
        .sda_io_num = CONFIG_TOUCH_SDA_GPIO,
        .scl_io_num = CONFIG_TOUCH_SCL_GPIO,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    // Probe GT911 address.  Valid addresses are 0x14 and 0x5D.
    // Using the new I2C master probe API
    uint8_t addr_found = 0;
    uint8_t addresses[2] = {0x14, 0x5D};
    for (size_t i = 0; i < 2; ++i) {
        esp_err_t ret = i2c_master_probe(i2c_bus, addresses[i], pdMS_TO_TICKS(50));
        if (ret == ESP_OK) {
            addr_found = addresses[i];
            break;
        }
    }
    if (addr_found == 0) {
        ESP_LOGE(TAG, "GT911 not detected on I2C bus, using default 0x14");
        addr_found = 0x14;
    } else {
        ESP_LOGI(TAG, "GT911 detected at address 0x%02X", addr_found);
    }

    // Create the panel IO handle for the touch device using new API
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = addr_found,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &touch_io));

    // Configure the GT911 driver
    esp_lcd_touch_config_t touch_cfg = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = CONFIG_TOUCH_RST_GPIO,
        .int_gpio_num = CONFIG_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0
        }
    };
    // Initialise the GT911 driver
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(touch_io, &touch_cfg, &touch_handle));

    // Create a mutex for protecting shared state
    touch_mutex = xSemaphoreCreateMutex();
    assert(touch_mutex);

    // Register LVGL input device (LVGL 9.x API)
    touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, lvgl_touch_read_cb);

    ESP_LOGI(TAG, "Touch controller initialized successfully");
}

void sensor_task(void *arg)
{
    (void)arg;
    while (1) {
        // Poll the GT911 for new data
        esp_lcd_touch_read_data(touch_handle);

        // Get touch data using new API
        uint16_t touch_x[1], touch_y[1];
        uint8_t touch_count = 0;
        bool pressed = esp_lcd_touch_get_data(touch_handle, touch_x, touch_y, NULL, &touch_count, 1);

        if (xSemaphoreTake(touch_mutex, portMAX_DELAY) == pdTRUE) {
            if (pressed && touch_count > 0) {
                s_touch_x = touch_x[0];
                s_touch_y = touch_y[0];
                s_touch_pressed = true;
            } else {
                s_touch_pressed = false;
            }
            xSemaphoreGive(touch_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
