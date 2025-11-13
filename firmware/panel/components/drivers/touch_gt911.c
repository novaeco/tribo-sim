#include "touch_gt911.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_log.h"

#include "display_driver.h"

#define TAG "gt911"

#define GT911_I2C_PORT         I2C_NUM_0
#define GT911_I2C_SDA          19
#define GT911_I2C_SCL          20
#define GT911_I2C_FREQ_HZ      400000
#define GT911_RST_PIN          38
#define GT911_INT_PIN          48

#define GT911_ADDR1            0x5D
#define GT911_ADDR2            0x14

#define GT911_PRODUCT_ID_REG   0x8140
#define GT911_STATUS_REG       0x814E
#define GT911_POINTS_REG       0x8150

#define GT911_STATUS_BUFFER_READY   0x80

static lv_indev_t *s_touch_indev = NULL;
static i2c_master_bus_handle_t s_gt_bus = NULL;
static i2c_master_dev_handle_t s_gt_dev = NULL;
static uint8_t s_gt_addr = GT911_ADDR1;

static esp_err_t gt911_bus_init(void)
{
    if (s_gt_bus) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = GT911_I2C_PORT,
        .sda_io_num = GT911_I2C_SDA,
        .scl_io_num = GT911_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    return i2c_new_master_bus(&bus_cfg, &s_gt_bus);
}

static esp_err_t gt911_add_device(uint8_t addr, i2c_master_dev_handle_t *out_dev)
{
    if (!out_dev) {
        return ESP_ERR_INVALID_ARG;
    }
    const i2c_device_config_t dev_cfg = {
        .device_address = addr,
        .scl_speed_hz = GT911_I2C_FREQ_HZ,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7BIT,
    };
    return i2c_master_bus_add_device(s_gt_bus, &dev_cfg, out_dev);
}

static esp_err_t gt911_i2c_read_dev(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_buf[2] = {(uint8_t)(reg & 0xFF), (uint8_t)(reg >> 8)};
    esp_err_t err = i2c_master_transmit_receive(dev, reg_buf, sizeof(reg_buf), data, len, pdMS_TO_TICKS(100));
    if (err == ESP_OK || err == ESP_ERR_TIMEOUT) {
        return err;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t gt911_i2c_write_dev(i2c_master_dev_handle_t dev, uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t payload[2 + 8];
    uint8_t *buf = payload;
    size_t total = len + 2;
    bool allocated = false;
    if (total > sizeof(payload)) {
        buf = malloc(total);
        if (!buf) {
            return ESP_ERR_NO_MEM;
        }
        allocated = true;
    }
    buf[0] = (uint8_t)(reg & 0xFF);
    buf[1] = (uint8_t)(reg >> 8);
    if (len && data) {
        memcpy(&buf[2], data, len);
    }
    esp_err_t err = i2c_master_transmit(dev, buf, total, pdMS_TO_TICKS(100));
    if (allocated) {
        free(buf);
    }
    if (err == ESP_OK || err == ESP_ERR_TIMEOUT) {
        return err;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t gt911_hw_reset(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(GT911_RST_PIN) | BIT64(GT911_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "GPIO config failed");

    ESP_RETURN_ON_ERROR(gpio_set_level(GT911_INT_PIN, 0), TAG, "INT low failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(GT911_RST_PIN, 0), TAG, "RST low failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(GT911_RST_PIN, 1), TAG, "RST high failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_direction(GT911_INT_PIN, GPIO_MODE_INPUT), TAG, "INT input failed");
    return ESP_OK;
}

static esp_err_t gt911_identify_address(void)
{
    static const uint8_t addresses[] = {GT911_ADDR1, GT911_ADDR2};
    uint8_t id[4] = {0};
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        i2c_master_dev_handle_t candidate = NULL;
        esp_err_t err = gt911_add_device(addresses[i], &candidate);
        if (err != ESP_OK) {
            continue;
        }
        err = gt911_i2c_read_dev(candidate, GT911_PRODUCT_ID_REG, id, sizeof(id));
        if (err == ESP_OK) {
            s_gt_dev = candidate;
            s_gt_addr = addresses[i];
            ESP_LOGI(TAG, "GT911 detected at 0x%02X (ID %02X%02X%02X%02X)", s_gt_addr, id[0], id[1], id[2], id[3]);
            return ESP_OK;
        }
        i2c_master_bus_rm_device(candidate);
    }
    return ESP_FAIL;
}

static esp_err_t gt911_i2c_read(uint16_t reg, uint8_t *data, size_t len)
{
    if (!s_gt_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return gt911_i2c_read_dev(s_gt_dev, reg, data, len);
}

static esp_err_t gt911_i2c_write(uint16_t reg, const uint8_t *data, size_t len)
{
    if (!s_gt_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return gt911_i2c_write_dev(s_gt_dev, reg, data, len);
}

static void gt911_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    uint8_t status = 0;
    if (gt911_i2c_read(GT911_STATUS_REG, &status, 1) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (!(status & GT911_STATUS_BUFFER_READY)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint8_t points = status & 0x0F;
    if (points == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
    } else {
        uint8_t buf[8] = {0};
        if (gt911_i2c_read(GT911_POINTS_REG, buf, sizeof(buf)) == ESP_OK) {
            uint16_t x = ((uint16_t)buf[1] << 8) | buf[0];
            uint16_t y = ((uint16_t)buf[3] << 8) | buf[2];
            if (x >= PANEL_H_RES) {
                x = PANEL_H_RES - 1;
            }
            if (y >= PANEL_V_RES) {
                y = PANEL_V_RES - 1;
            }
            data->point.x = (lv_coord_t)x;
            data->point.y = (lv_coord_t)y;
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }

    const uint8_t clear = 0;
    if (gt911_i2c_write(GT911_STATUS_REG, &clear, 1) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear GT911 status");
    }
}

esp_err_t touch_gt911_init(lv_disp_t *disp, lv_indev_t **indev_out)
{
    ESP_RETURN_ON_FALSE(disp != NULL, ESP_ERR_INVALID_ARG, TAG, "Display handle required");
    ESP_RETURN_ON_ERROR(gt911_bus_init(), TAG, "Failed to initialize I2C bus");
    ESP_RETURN_ON_ERROR(gt911_hw_reset(), TAG, "Touch reset failed");
    ESP_RETURN_ON_ERROR(gt911_identify_address(), TAG, "GT911 not detected");

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = gt911_read_cb;
    s_touch_indev = lv_indev_drv_register(&indev_drv);
    ESP_RETURN_ON_FALSE(s_touch_indev != NULL, ESP_ERR_NO_MEM, TAG, "Failed to register LVGL touch input");

    if (indev_out) {
        *indev_out = s_touch_indev;
    }
    return ESP_OK;
}
