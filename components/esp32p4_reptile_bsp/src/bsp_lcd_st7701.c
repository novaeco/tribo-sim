/**
 * @file bsp_lcd_st7701.c
 * @brief Minimal ST7701 MIPI-DSI LCD Driver (JD9165BA compatible)
 *
 * This is a minimal implementation to avoid version conflicts with managed components.
 * Based on ESP-IDF's panel vendor API.
 */

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "ST7701";

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    uint8_t madctl_val;
    uint8_t colmod_val;
    const esp_lcd_panel_dev_config_t *dev_config;
} st7701_panel_t;

// ST7701 specific commands
#define ST7701_CMD_CND2BKxSEL   0xFF  // Command2 BKx selection
#define ST7701_CMD_BK0_PVGAMCTRL 0xB0  // Positive Voltage Gamma Control
#define ST7701_CMD_BK0_NVGAMCTRL 0xB1  // Negative Voltage Gamma Control
#define ST7701_CMD_BK1_VRHS      0xB0  // Vop amplitude setting
#define ST7701_CMD_BK1_VCOM      0xB1  // VCOM amplitude setting
#define ST7701_CMD_BK1_VGHSS     0xB2  // VGH Clamp Level
#define ST7701_CMD_BK1_TESTCMD   0xB3  // TEST Command Setting
#define ST7701_CMD_BK1_VGLS      0xB5  // VGL Clamp Level
#define ST7701_CMD_BK1_PWCTLR1   0xB7  // Power Control 1
#define ST7701_CMD_BK1_PWCTLR2   0xB8  // Power Control 2
#define ST7701_CMD_BK1_SPD1      0xC1  // Source pre_drive timing set1
#define ST7701_CMD_BK1_SPD2      0xC2  // Source EQ2 Setting
#define ST7701_CMD_BK1_MIPISET1  0xD0  // MIPI Setting 1

static esp_err_t panel_st7701_reset(esp_lcd_panel_t *panel)
{
    st7701_panel_t *st7701 = __containerof(panel, st7701_panel_t, base);

    // Hardware reset
    if (st7701->reset_gpio_num >= 0) {
        gpio_set_level(st7701->reset_gpio_num, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(st7701->reset_gpio_num, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

static esp_err_t panel_st7701_init(esp_lcd_panel_t *panel)
{
    st7701_panel_t *st7701 = __containerof(panel, st7701_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7701->io;

    // Sleep out
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    // Command2 BKx Selection: Enable Command2 Part1
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_CND2BKxSEL, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5), TAG, "send command failed");

    // Power Control registers
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_VRHS, (uint8_t[]){0x4D}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_VCOM, (uint8_t[]){0x43}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_VGHSS, (uint8_t[]){0x07}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_TESTCMD, (uint8_t[]){0x80}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_VGLS, (uint8_t[]){0x49}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_PWCTLR1, (uint8_t[]){0x85}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_PWCTLR2, (uint8_t[]){0x20}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_SPD1, (uint8_t[]){0x78}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_SPD2, (uint8_t[]){0x78}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_BK1_MIPISET1, (uint8_t[]){0x88}, 1), TAG, "send command failed");

    vTaskDelay(pdMS_TO_TICKS(100));

    // Disable Command2
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ST7701_CMD_CND2BKxSEL, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5), TAG, "send command failed");

    // Display on
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_DISPON, NULL, 0), TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "ST7701 panel initialized");
    return ESP_OK;
}

static esp_err_t panel_st7701_del(esp_lcd_panel_t *panel)
{
    st7701_panel_t *st7701 = __containerof(panel, st7701_panel_t, base);

    if (st7701->reset_gpio_num >= 0) {
        gpio_reset_pin(st7701->reset_gpio_num);
    }

    free(st7701);
    return ESP_OK;
}

static esp_err_t panel_st7701_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    st7701_panel_t *st7701 = __containerof(panel, st7701_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7701->io;

    if (mirror_x) {
        st7701->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        st7701->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        st7701->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        st7701->madctl_val &= ~LCD_CMD_MY_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, &st7701->madctl_val, 1), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_st7701_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    st7701_panel_t *st7701 = __containerof(panel, st7701_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7701->io;

    if (swap_axes) {
        st7701->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        st7701->madctl_val &= ~LCD_CMD_MV_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, &st7701->madctl_val, 1), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_st7701_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    // ST7701 doesn't support gap setting
    return ESP_OK;
}

static esp_err_t panel_st7701_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    st7701_panel_t *st7701 = __containerof(panel, st7701_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7701->io;
    int command = invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_st7701_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    st7701_panel_t *st7701 = __containerof(panel, st7701_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7701->io;
    int command = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_st7701(esp_lcd_dsi_bus_handle_t dsi_bus, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(dsi_bus && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    st7701_panel_t *st7701 = calloc(1, sizeof(st7701_panel_t));
    ESP_RETURN_ON_FALSE(st7701, ESP_ERR_NO_MEM, TAG, "no mem for st7701 panel");

    st7701->reset_gpio_num = panel_dev_config->reset_gpio_num;
    st7701->dev_config = panel_dev_config;
    st7701->madctl_val = 0;
    st7701->colmod_val = 0x55; // RGB565

    // Configure reset GPIO
    if (st7701->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << st7701->reset_gpio_num),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    // Create MIPI DBI panel IO
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &st7701->io), err, TAG, "create panel IO failed");

    // Fill in panel  interface
    st7701->base.del = panel_st7701_del;
    st7701->base.reset = panel_st7701_reset;
    st7701->base.init = panel_st7701_init;
    st7701->base.invert_color = panel_st7701_invert_color;
    st7701->base.mirror = panel_st7701_mirror;
    st7701->base.swap_xy = panel_st7701_swap_xy;
    st7701->base.set_gap = panel_st7701_set_gap;
    st7701->base.disp_on_off = panel_st7701_disp_on_off;

    *ret_panel = &st7701->base;
    ESP_LOGI(TAG, "ST7701 panel created @%p", st7701);
    return ESP_OK;

err:
    if (st7701) {
        if (st7701->reset_gpio_num >= 0) {
            gpio_reset_pin(st7701->reset_gpio_num);
        }
        free(st7701);
    }
    return ret;
}
