#include "touch_gt911.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_bit_defs.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_check.h"
#include "display_driver.h"

#define TAG "gt911"

#define GT911_I2C_NUM         I2C_NUM_0
#define GT911_I2C_SDA         19
#define GT911_I2C_SCL         20
#define GT911_I2C_FREQ_HZ     400000
#define GT911_RST_PIN         38
#define GT911_INT_PIN         48

#define GT911_ADDR1           0x5D
#define GT911_ADDR2           0x14

#define GT911_PRODUCT_ID_REG  0x8140
#define GT911_STATUS_REG      0x814E
#define GT911_POINTS_REG      0x8150

#define GT911_STATUS_BUFFER_READY   0x80

static uint8_t s_gt911_addr = GT911_ADDR1;
static lv_indev_t *s_touch_indev;

static esp_err_t gt911_i2c_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_buf[2] = { reg >> 8, reg & 0xFF };
    uint8_t reg_buf[2] = { reg & 0xFF, reg >> 8 };
    esp_err_t err = i2c_master_write_read_device(GT911_I2C_NUM, s_gt911_addr, reg_buf, sizeof(reg_buf), data, len, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c read 0x%04x failed (%s)", reg, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t gt911_i2c_write(uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[2 + len];
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    buf[0] = reg & 0xFF;
    buf[1] = reg >> 8;
    memcpy(&buf[2], data, len);
    return i2c_master_write_to_device(GT911_I2C_NUM, s_gt911_addr, buf, sizeof(buf), pdMS_TO_TICKS(100));
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
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(GT911_INT_PIN, 0);
    gpio_set_level(GT911_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(GT911_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_direction(GT911_INT_PIN, GPIO_MODE_INPUT);
    return ESP_OK;
}

static esp_err_t gt911_identify_address(void)
{
    uint8_t buffer[4] = {0};
    const uint8_t addresses[] = {GT911_ADDR1, GT911_ADDR2};
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        s_gt911_addr = addresses[i];
        if (gt911_i2c_read(GT911_PRODUCT_ID_REG, buffer, sizeof(buffer)) == ESP_OK) {
            ESP_LOGI(TAG, "GT911 detected at 0x%02x (ID %02x%02x%02x%02x)", s_gt911_addr, buffer[0], buffer[1], buffer[2], buffer[3]);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
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
        uint8_t buf[8];
        if (gt911_i2c_read(GT911_POINTS_REG, buf, sizeof(buf)) == ESP_OK) {
            uint16_t x = buf[1] << 8 | buf[0];
            uint16_t y = buf[3] << 8 | buf[2];
            if (x >= PANEL_H_RES) {
                x = PANEL_H_RES - 1;
            }
            if (y >= PANEL_V_RES) {
                y = PANEL_V_RES - 1;
            }
            data->point.x = (lv_coord_t)x;
            data->point.y = (lv_coord_t)y;
            data->point.x = x;
            data->point.y = y;
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

    gt911_i2c_write(GT911_STATUS_REG, &clear, 1);
}

esp_err_t touch_gt911_init(lv_indev_t **indev_out)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GT911_I2C_SDA,
        .scl_io_num = GT911_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = GT911_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(GT911_I2C_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(GT911_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = gt911_hw_reset();
    if (err != ESP_OK) {
        i2c_driver_delete(GT911_I2C_NUM);
        return err;
    }

    err = gt911_identify_address();
    if (err != ESP_OK) {
        i2c_driver_delete(GT911_I2C_NUM);
        return err;
    }
    ESP_ERROR_CHECK(i2c_param_config(GT911_I2C_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(GT911_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));

    ESP_ERROR_CHECK(gt911_hw_reset());

    ESP_RETURN_ON_ERROR(gt911_identify_address(), TAG, "Failed to detect GT911");

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = gt911_read_cb;
    indev_drv.disp = disp;
    s_touch_indev = lv_indev_drv_register(&indev_drv);
    if (!s_touch_indev) {
        i2c_driver_delete(GT911_I2C_NUM);
        return ESP_ERR_NO_MEM;
    }
    s_touch_indev = lv_indev_drv_register(&indev_drv);

    if (indev_out) {
        *indev_out = s_touch_indev;
    }

    ESP_LOGI(TAG, "GT911 touch initialized");
    return ESP_OK;
}
