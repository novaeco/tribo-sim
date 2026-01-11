// SPDX-License-Identifier: Apache-2.0
//
// Implementation of the display subsystem for the reptile simulation.
//
// This component handles the bring-up of the JD9165 LCD panel over
// MIPI-DSI, integration with the esp_lcd driver and LVGL 9.x, and
// management of the main UI screens with improved visuals.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "lvgl.h"
#include "sim_display.h"
#include "game.h"

static const char *TAG = "SIM_DISPLAY";

// Display configuration
#define LCD_H_RES       1024
#define LCD_V_RES       600
#define LCD_BIT_PER_PIXEL  16
#define LCD_NUM_FB      2

// MIPI-DSI timing from JD9165 dtsi file
#define MIPI_DSI_LANE_NUM          2
#define MIPI_DSI_LANE_BITRATE_MBPS 500

// JD9165 timing parameters (from dtsi)
#define JD9165_HSYNC    24
#define JD9165_HBP      136
#define JD9165_HFP      160
#define JD9165_VSYNC    2
#define JD9165_VBP      21
#define JD9165_VFP      12
#define JD9165_PCLK_MHZ 51

// LVGL objects created in create_ui()
lv_obj_t *pet_obj = NULL;
lv_obj_t *label_status = NULL;
lv_obj_t *label_perf = NULL;

// New UI elements for improved visuals
static lv_obj_t *health_bar = NULL;
static lv_obj_t *hunger_bar = NULL;
static lv_obj_t *growth_bar = NULL;
static lv_obj_t *temp_arc = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *screen_home = NULL;
static lv_obj_t *screen_game = NULL;

// Handle to the LCD panel
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
static lv_display_t *lvgl_disp = NULL;

// Frame buffers
static void *frame_buffer[LCD_NUM_FB] = {NULL, NULL};

// Counters used for FPS measurement
static uint32_t frame_count = 0;

// Forward declarations
static void update_status_label_async(void *param);
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void start_btn_event_cb(lv_event_t *e);
static void feed_btn_event_cb(lv_event_t *e);
static void heat_btn_event_cb(lv_event_t *e);
static void pause_btn_event_cb(lv_event_t *e);
static void play_btn_event_cb(lv_event_t *e);
static void clean_btn_event_cb(lv_event_t *e);

// JD9165 initialization commands (from dtsi file)
static const uint8_t jd9165_init_cmds[][16] = {
    {0x02, 0x30, 0x00},
    {0x05, 0xF7, 0x49, 0x61, 0x02, 0x00},
    {0x02, 0x30, 0x01},
    {0x02, 0x04, 0x0C},
    {0x02, 0x05, 0x00},
    {0x02, 0x06, 0x00},
    {0x02, 0x0B, 0x11},  // 2 lanes
    {0x02, 0x17, 0x00},
    {0x02, 0x20, 0x04},
    {0x02, 0x1F, 0x05},
    {0x02, 0x23, 0x00},
    {0x02, 0x25, 0x19},
    {0x02, 0x28, 0x18},
    {0x02, 0x29, 0x04},
    {0x02, 0x2A, 0x01},
    {0x02, 0x2B, 0x04},
    {0x02, 0x2C, 0x01},
    {0x02, 0x30, 0x02},
    {0x02, 0x01, 0x22},
    {0x02, 0x03, 0x12},
    {0x02, 0x04, 0x00},
    {0x02, 0x05, 0x64},
    {0x02, 0x0A, 0x08},
    {0x02, 0x30, 0x06},
    {0x02, 0x30, 0x0A},
    {0x02, 0x02, 0x4F},
    {0x02, 0x0B, 0x40},
    {0x02, 0x12, 0x3E},
    {0x02, 0x13, 0x78},
    {0x02, 0x30, 0x0D},
    {0x02, 0x0D, 0x04},
    {0x02, 0x10, 0x0C},
    {0x02, 0x11, 0x0C},
    {0x02, 0x12, 0x0C},
    {0x02, 0x13, 0x0C},
    {0x02, 0x30, 0x00},
    {0x01, 0x11},  // Sleep out
    {0x00},        // End marker
};

// Helper to drive PWM backlight
static void backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 25000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = CONFIG_LCD_BL_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel_conf);
}

static void backlight_set(uint8_t brightness_percent)
{
    uint32_t duty = (1023 * brightness_percent) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// Send initialization commands to JD9165 via DCS
static void jd9165_send_init_cmds(esp_lcd_panel_io_handle_t io)
{
    for (int i = 0; jd9165_init_cmds[i][0] != 0; i++) {
        uint8_t len = jd9165_init_cmds[i][0];
        uint8_t cmd = jd9165_init_cmds[i][1];
        const uint8_t *data = &jd9165_init_cmds[i][2];
        esp_lcd_panel_io_tx_param(io, cmd, data, len - 1);
        if (cmd == 0x11 || cmd == 0x29) {
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
    // Display on
    esp_lcd_panel_io_tx_param(io, 0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
}

void display_init_panel(void)
{
    ESP_LOGI(TAG, "Initializing MIPI-DSI bus");

    // Initialize backlight
    backlight_init();

    // Reset LCD via GPIO
    gpio_config_t rst_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << CONFIG_LCD_RESET_GPIO
    };
    gpio_config(&rst_cfg);
    gpio_set_level(CONFIG_LCD_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CONFIG_LCD_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Create MIPI-DSI bus
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus));

    // Create DBI IO for sending commands
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &io_handle));

    // Send JD9165 initialization commands
    ESP_LOGI(TAG, "Sending JD9165 init commands");
    jd9165_send_init_cmds(io_handle);

    // Create DPI panel for video stream
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = JD9165_PCLK_MHZ,
        .num_fbs = LCD_NUM_FB,
        .video_timing = {
            .h_size = LCD_H_RES,
            .v_size = LCD_V_RES,
            .hsync_back_porch = JD9165_HBP,
            .hsync_pulse_width = JD9165_HSYNC,
            .hsync_front_porch = JD9165_HFP,
            .vsync_back_porch = JD9165_VBP,
            .vsync_pulse_width = JD9165_VSYNC,
            .vsync_front_porch = JD9165_VFP,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(dsi_bus, &dpi_config, &lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));

    // Turn on backlight
    backlight_set(100);
    ESP_LOGI(TAG, "Display initialized successfully");
}

// LVGL flush callback for LVGL 9.x API
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    esp_lcd_panel_draw_bitmap(lcd_panel, x1, y1, x2 + 1, y2 + 1, px_map);
    frame_count++;

    lv_display_flush_ready(disp);
}

void lvgl_start(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();

    // Allocate frame buffers in PSRAM
    size_t fb_size = LCD_H_RES * LCD_V_RES * sizeof(lv_color16_t);
    for (int i = 0; i < LCD_NUM_FB; i++) {
        frame_buffer[i] = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        assert(frame_buffer[i] != NULL);
        ESP_LOGI(TAG, "Frame buffer %d allocated: %p (%zu bytes)", i, frame_buffer[i], fb_size);
    }

    // Create display (LVGL 9.x API)
    lvgl_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(lvgl_disp, flush_callback);
    lv_display_set_buffers(lvgl_disp, frame_buffer[0], frame_buffer[1], fb_size, LV_DISPLAY_RENDER_MODE_FULL);

    ESP_LOGI(TAG, "LVGL initialized with %dx%d display", LCD_H_RES, LCD_V_RES);
}

// Create a styled button helper
static lv_obj_t *create_styled_button(lv_obj_t *parent, const char *text,
                                       lv_coord_t w, lv_coord_t h,
                                       lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B5E20), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 5, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    return btn;
}

// Create progress bar with label
static lv_obj_t *create_stat_bar(lv_obj_t *parent, const char *label_text,
                                  lv_coord_t x, lv_coord_t y,
                                  lv_color_t color)
{
    // Container
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 180, 45);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Label
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    // Progress bar
    lv_obj_t *bar = lv_bar_create(cont);
    lv_obj_set_size(bar, 170, 15);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x424242), 0);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);

    return bar;
}

void create_ui(void)
{
    // ========== HOME SCREEN ==========
    screen_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_home, lv_color_hex(0x1A237E), 0);

    // Title
    lv_obj_t *title = lv_label_create(screen_home);
    lv_label_set_text(title, "Terrarium Reptile");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    // Subtitle
    lv_obj_t *subtitle = lv_label_create(screen_home);
    lv_label_set_text(subtitle, "Prenez soin de votre reptile virtuel!");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xB0BEC5), 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, -40);

    // Reptile ASCII art placeholder
    lv_obj_t *art = lv_label_create(screen_home);
    lv_label_set_text(art,
        "     __\n"
        " .-.'  `; `-._\n"
        "(_,         .-:\n"
        " ,'o\"(        )\n"
        "(__,-'      ,'\n"
        "   (googly) ");
    lv_obj_set_style_text_color(art, lv_color_hex(0x66BB6A), 0);
    lv_obj_set_style_text_font(art, &lv_font_montserrat_14, 0);
    lv_obj_align(art, LV_ALIGN_CENTER, 0, 40);

    // Start button
    lv_obj_t *btn_start = create_styled_button(screen_home, "Commencer", 200, 60,
                                                start_btn_event_cb, NULL);
    lv_obj_align(btn_start, LV_ALIGN_CENTER, 0, 140);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x4CAF50), 0);

    // ========== GAME SCREEN ==========
    screen_game = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_game, lv_color_hex(0x263238), 0);

    // Stats panel (left side)
    lv_obj_t *stats_panel = lv_obj_create(screen_game);
    lv_obj_set_size(stats_panel, 200, 280);
    lv_obj_set_pos(stats_panel, 10, 10);
    lv_obj_set_style_bg_color(stats_panel, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_radius(stats_panel, 15, 0);
    lv_obj_set_style_border_width(stats_panel, 2, 0);
    lv_obj_set_style_border_color(stats_panel, lv_color_hex(0x4CAF50), 0);
    lv_obj_clear_flag(stats_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Stats title
    lv_obj_t *stats_title = lv_label_create(stats_panel);
    lv_label_set_text(stats_title, "Statistiques");
    lv_obj_set_style_text_color(stats_title, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_text_font(stats_title, &lv_font_montserrat_14, 0);
    lv_obj_align(stats_title, LV_ALIGN_TOP_MID, 0, 5);

    // Health bar (green)
    health_bar = create_stat_bar(stats_panel, "Sante", 10, 35, lv_color_hex(0x4CAF50));
    lv_bar_set_value(health_bar, 100, LV_ANIM_OFF);

    // Hunger bar (orange)
    hunger_bar = create_stat_bar(stats_panel, "Faim", 10, 90, lv_color_hex(0xFF9800));
    lv_bar_set_value(hunger_bar, 0, LV_ANIM_OFF);

    // Growth bar (blue)
    growth_bar = create_stat_bar(stats_panel, "Croissance", 10, 145, lv_color_hex(0x2196F3));
    lv_bar_set_value(growth_bar, 0, LV_ANIM_OFF);

    // Temperature arc
    temp_arc = lv_arc_create(stats_panel);
    lv_obj_set_size(temp_arc, 80, 80);
    lv_obj_align(temp_arc, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_arc_set_range(temp_arc, 15, 40);
    lv_arc_set_value(temp_arc, 25);
    lv_arc_set_bg_angles(temp_arc, 135, 45);
    lv_obj_set_style_arc_color(temp_arc, lv_color_hex(0x424242), LV_PART_MAIN);
    lv_obj_set_style_arc_color(temp_arc, lv_color_hex(0xF44336), LV_PART_INDICATOR);
    lv_obj_remove_flag(temp_arc, LV_OBJ_FLAG_CLICKABLE);

    temp_label = lv_label_create(temp_arc);
    lv_label_set_text(temp_label, "25.0C");
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(temp_label);

    // Terrarium area (center)
    lv_obj_t *terrarium = lv_obj_create(screen_game);
    lv_obj_set_size(terrarium, 580, 400);
    lv_obj_set_pos(terrarium, 220, 10);
    lv_obj_set_style_bg_color(terrarium, lv_color_hex(0x1B5E20), 0);
    lv_obj_set_style_radius(terrarium, 20, 0);
    lv_obj_set_style_border_width(terrarium, 3, 0);
    lv_obj_set_style_border_color(terrarium, lv_color_hex(0x795548), 0);
    lv_obj_clear_flag(terrarium, LV_OBJ_FLAG_SCROLLABLE);

    // Ground decoration
    lv_obj_t *ground = lv_obj_create(terrarium);
    lv_obj_set_size(ground, 560, 80);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(ground, lv_color_hex(0x5D4037), 0);
    lv_obj_set_style_radius(ground, 10, 0);
    lv_obj_set_style_border_width(ground, 0, 0);

    // Pet object (reptile representation)
    pet_obj = lv_obj_create(terrarium);
    lv_obj_set_size(pet_obj, 80, 50);
    lv_obj_align(pet_obj, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(pet_obj, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_radius(pet_obj, 25, 0);
    lv_obj_set_style_border_width(pet_obj, 2, 0);
    lv_obj_set_style_border_color(pet_obj, lv_color_hex(0x2E7D32), 0);
    lv_obj_clear_flag(pet_obj, LV_OBJ_FLAG_SCROLLABLE);

    // Eyes for the pet
    lv_obj_t *eye1 = lv_obj_create(pet_obj);
    lv_obj_set_size(eye1, 12, 12);
    lv_obj_set_pos(eye1, 15, 10);
    lv_obj_set_style_bg_color(eye1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(eye1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(eye1, 0, 0);

    lv_obj_t *pupil1 = lv_obj_create(eye1);
    lv_obj_set_size(pupil1, 6, 6);
    lv_obj_center(pupil1);
    lv_obj_set_style_bg_color(pupil1, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(pupil1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pupil1, 0, 0);

    lv_obj_t *eye2 = lv_obj_create(pet_obj);
    lv_obj_set_size(eye2, 12, 12);
    lv_obj_set_pos(eye2, 50, 10);
    lv_obj_set_style_bg_color(eye2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(eye2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(eye2, 0, 0);

    lv_obj_t *pupil2 = lv_obj_create(eye2);
    lv_obj_set_size(pupil2, 6, 6);
    lv_obj_center(pupil2);
    lv_obj_set_style_bg_color(pupil2, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(pupil2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pupil2, 0, 0);

    // Hidden status label for compatibility
    label_status = lv_label_create(screen_game);
    lv_obj_add_flag(label_status, LV_OBJ_FLAG_HIDDEN);

    // Control buttons (bottom)
    lv_obj_t *btn_panel = lv_obj_create(screen_game);
    lv_obj_set_size(btn_panel, 780, 70);
    lv_obj_align(btn_panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn_panel, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_radius(btn_panel, 15, 0);
    lv_obj_set_style_border_width(btn_panel, 0, 0);
    lv_obj_clear_flag(btn_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Action buttons
    lv_obj_t *btn_feed = create_styled_button(btn_panel, "Nourrir", 110, 50, feed_btn_event_cb, NULL);
    lv_obj_set_style_bg_color(btn_feed, lv_color_hex(0xFF9800), 0);

    static lv_obj_t *heat_lbl;
    lv_obj_t *btn_heat = create_styled_button(btn_panel, "Chauffage", 120, 50, heat_btn_event_cb, NULL);
    heat_lbl = lv_obj_get_child(btn_heat, 0);
    lv_obj_set_user_data(btn_heat, heat_lbl);
    lv_obj_set_style_bg_color(btn_heat, lv_color_hex(0xF44336), 0);

    lv_obj_t *btn_play = create_styled_button(btn_panel, "Jouer", 100, 50, play_btn_event_cb, NULL);
    lv_obj_set_style_bg_color(btn_play, lv_color_hex(0x9C27B0), 0);

    lv_obj_t *btn_clean = create_styled_button(btn_panel, "Nettoyer", 110, 50, clean_btn_event_cb, NULL);
    lv_obj_set_style_bg_color(btn_clean, lv_color_hex(0x00BCD4), 0);

    static lv_obj_t *pause_lbl;
    lv_obj_t *btn_pause = create_styled_button(btn_panel, "Pause", 100, 50, pause_btn_event_cb, NULL);
    pause_lbl = lv_obj_get_child(btn_pause, 0);
    lv_obj_set_user_data(btn_pause, pause_lbl);
    lv_obj_set_style_bg_color(btn_pause, lv_color_hex(0x607D8B), 0);

    // Performance label (top right)
    label_perf = lv_label_create(screen_game);
    lv_obj_align(label_perf, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_label_set_text(label_perf, "FPS: --");
    lv_obj_set_style_text_color(label_perf, lv_color_hex(0x9E9E9E), 0);

    // Load home screen first
    lv_screen_load(screen_home);
}

void display_update_status_async(const char *status)
{
    if (!label_status) {
        return;
    }
    char *copy = strdup(status);
    if (copy) {
        lv_async_call(update_status_label_async, copy);
    }
}

// Update UI elements based on game state
void display_update_game_state(const ReptileState *state)
{
    if (health_bar) {
        lv_bar_set_value(health_bar, state->health, LV_ANIM_ON);
        // Change color based on health
        if (state->health < 30) {
            lv_obj_set_style_bg_color(health_bar, lv_color_hex(0xF44336), LV_PART_INDICATOR);
        } else if (state->health < 60) {
            lv_obj_set_style_bg_color(health_bar, lv_color_hex(0xFFC107), LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_bg_color(health_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
        }
    }
    if (hunger_bar) {
        lv_bar_set_value(hunger_bar, state->hunger, LV_ANIM_ON);
    }
    if (growth_bar) {
        lv_bar_set_value(growth_bar, state->growth, LV_ANIM_ON);
    }
    if (temp_arc) {
        lv_arc_set_value(temp_arc, (int)state->temperature);
        // Update color based on temperature range
        if (state->temperature >= 26.0f && state->temperature <= 32.0f) {
            lv_obj_set_style_arc_color(temp_arc, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_arc_color(temp_arc, lv_color_hex(0xF44336), LV_PART_INDICATOR);
        }
    }
    if (temp_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fC", state->temperature);
        lv_label_set_text(temp_label, buf);
    }

    // Update pet appearance based on state
    if (pet_obj) {
        if (state->health < 30) {
            lv_obj_set_style_bg_color(pet_obj, lv_color_hex(0x795548), 0); // Brown when sick
        } else if (state->hunger > 70) {
            lv_obj_set_style_bg_color(pet_obj, lv_color_hex(0x8BC34A), 0); // Light green when hungry
        } else {
            lv_obj_set_style_bg_color(pet_obj, lv_color_hex(0x4CAF50), 0); // Normal green
        }
    }
}

static void update_status_label_async(void *param)
{
    char *text = (char *)param;
    if (text) {
        // Parse values and update visual elements
        int health, hunger;
        float temp;
        if (sscanf(text, "Sante: %d\nFaim: %d\nTemp: %f", &health, &hunger, &temp) == 3) {
            ReptileState state = {
                .health = health,
                .hunger = hunger,
                .temperature = temp,
                .growth = g_state.growth,
                .heater_on = g_state.heater_on
            };
            display_update_game_state(&state);
        }
    }
    free(text);
}

// Button callbacks
static void start_btn_event_cb(lv_event_t *e)
{
    (void)e;
    lv_screen_load_anim(screen_game, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    game_started = true;
}

static void feed_btn_event_cb(lv_event_t *e)
{
    (void)e;
    game_post_event(GAME_EVENT_FEED);
}

static void heat_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_user_data(btn);
    static bool heater_on = false;
    heater_on = !heater_on;
    game_post_event(heater_on ? GAME_EVENT_HEAT_ON : GAME_EVENT_HEAT_OFF);
    if (lbl) {
        lv_label_set_text(lbl, heater_on ? "Chauff: ON" : "Chauffage");
        lv_obj_set_style_bg_color(btn, heater_on ? lv_color_hex(0xE53935) : lv_color_hex(0xF44336), 0);
    }
}

static void play_btn_event_cb(lv_event_t *e)
{
    (void)e;
    game_post_event(GAME_EVENT_PLAY);
}

static void clean_btn_event_cb(lv_event_t *e)
{
    (void)e;
    game_post_event(GAME_EVENT_CLEAN);
}

static void pause_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_user_data(btn);
    static bool paused = false;
    paused = !paused;
    game_post_event(paused ? GAME_EVENT_PAUSE : GAME_EVENT_RESUME);
    if (lbl) {
        lv_label_set_text(lbl, paused ? "Reprendre" : "Pause");
    }
}

void display_task(void *arg)
{
    (void)arg;
    TickType_t last_report = xTaskGetTickCount();
    uint32_t last_frame_count = 0;
    TickType_t delay = pdMS_TO_TICKS(5);

    while (1) {
        lv_timer_handler();

        // Update performance stats every second
        TickType_t now = xTaskGetTickCount();
        if (now - last_report >= pdMS_TO_TICKS(1000)) {
            uint32_t frames = frame_count;
            uint32_t fps = frames - last_frame_count;
            last_frame_count = frames;
            size_t free_heap = esp_get_free_heap_size();
            if (label_perf) {
                char buf[64];
                snprintf(buf, sizeof(buf), "FPS: %lu | RAM: %lu KB",
                         (unsigned long)fps, (unsigned long)(free_heap / 1024));
                lv_label_set_text(label_perf, buf);
            }
            last_report = now;
        }
        vTaskDelay(delay);
    }
}
