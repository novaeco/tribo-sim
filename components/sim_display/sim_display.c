// SPDX-License-Identifier: Apache-2.0
//
// Implementation of the display subsystem for the reptile simulation.
//
// This component handles the bring‑up of the JD9165 LCD panel over
// MIPI‑DSI, integration with the esp_lcd driver and LVGL, and
// management of the main UI screens.  It also includes the LVGL
// flush callback and instrumentation (FPS, memory usage, per‑task
// runtime stats).  UI event callbacks post game events via a
// helper in game.c.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
// Note: We deliberately avoid including esp_timer.h here to remove
// dependencies on the esp_timer component.  No esp_timer functions are
// used in this module.
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_dsi.h"
#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"
#include "sim_display.h"
#include "game.h"

static const char *TAG = "SIM_DISPLAY";

// LVGL objects created in create_ui().  They are exposed via
// the header so other components can modify them.  Declared here
// statically and initialised in create_ui().
lv_obj_t *pet_obj = NULL;
lv_obj_t *label_status = NULL;
lv_obj_t *label_perf = NULL;

// Handle to the LCD panel
static esp_lcd_panel_handle_t lcd_panel = NULL;

// Counters used for FPS measurement
static uint32_t frame_count = 0;

// Forward declaration of the async update callback.  It updates
// label_status and frees the provided string after use.
static void update_status_label_async(void *param);

// Helper to drive a GPIO high or low for the backlight
static void backlight_enable(bool on)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << CONFIG_LCD_BL_PWM_GPIO
    };
    gpio_config(&cfg);
    gpio_set_level(CONFIG_LCD_BL_PWM_GPIO, on ? 1 : 0);
}

void display_init_panel(void)
{
    // Note: We assume the board’s regulators are configured appropriately
    // for the JD9165 and omit explicit LDO setup.

    // Create the DSI bus with 2 lanes.  The JD9165 driver provides
    // convenient macros to fill the configuration structures.
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t dsi_bus_config = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi_bus_config, &dsi_bus));

    // Create a DBI IO handle on the DSI bus.  This abstracts the
    // low‑level command interface to the LCD panel.
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_dbi_io_config_t io_config = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &io_config, &panel_io));

    // Panel configuration: resolution and colour depth from the dtsi.
    const esp_lcd_dpi_panel_config_t dpi_config =
        JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(16);
    jd9165_vendor_config_t vendor_cfg = {
        .flags = { .use_mipi_interface = 1 },
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config
        }
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_LCD_RESET_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg
    };

    // Instantiate the panel driver
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(panel_io, &panel_config, &lcd_panel));
    // Hardware reset the panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    // Execute initialisation commands (sleep out, display on, etc.)
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));

    // Enable the backlight to full brightness
    backlight_enable(true);
}

void lvgl_start(void)
{
    // Initialize LVGL
    lv_init();

    // Allocate two frame buffers in PSRAM for double buffering
    static lv_color_t *buf1 = NULL;
    static lv_color_t *buf2 = NULL;
    buf1 = (lv_color_t *)heap_caps_malloc(1024 * 600 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t *)heap_caps_malloc(1024 * 600 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 1024 * 600);

    // Register the display driver with LVGL
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 1024;
    disp_drv.ver_res = 600;
    disp_drv.flush_cb = [](lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
        // Copy the area to the LCD via the driver
        int x1 = area->x1;
        int y1 = area->y1;
        int x2 = area->x2;
        int y2 = area->y2;
        esp_lcd_panel_draw_bitmap(lcd_panel, x1, y1, x2 + 1, y2 + 1, color_p);
        // Indicate to LVGL that flushing is finished
        lv_disp_flush_ready(drv);
        // Update the frame counter for FPS measurement
        frame_count++;
    };
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);
}

// Pointers to the screens created in create_ui().  We keep them
// static to avoid exposing them outside of this file.  The current
// screen loaded is managed by LVGL itself.
static lv_obj_t *screen_home = NULL;
static lv_obj_t *screen_game = NULL;

// Event callback prototypes (declared static so they cannot be
// referenced outside this translation unit).  They post events to the
// game logic via game_post_event().
static void start_btn_event_cb(lv_event_t *e);
static void feed_btn_event_cb(lv_event_t *e);
static void heat_btn_event_cb(lv_event_t *e);
static void pause_btn_event_cb(lv_event_t *e);

// Create the LVGL UI (home screen and game screen).  This function
// should only be called once after LVGL has been initialised.
void create_ui(void)
{
    // Home screen with a title and start button
    screen_home = lv_obj_create(NULL);
    lv_obj_t *label_title = lv_label_create(screen_home);
    lv_label_set_text(label_title, "Terrarium Reptile");
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *btn_start = lv_btn_create(screen_home);
    lv_obj_align(btn_start, LV_ALIGN_CENTER, 0, 20);
    lv_obj_t *lbl = lv_label_create(btn_start);
    lv_label_set_text(lbl, "Commencer");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn_start, start_btn_event_cb, LV_EVENT_CLICKED, NULL);

    // Game screen with status label, buttons and pet object
    screen_game = lv_obj_create(NULL);
    label_status = lv_label_create(screen_game);
    lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 10, 10);
    // Default status text
    lv_label_set_text(label_status, "Santé: 100\nFaim: 0\nTemp: 25.0°C");

    // Feed button
    lv_obj_t *btn_feed = lv_btn_create(screen_game);
    lv_obj_set_size(btn_feed, 100, 40);
    lv_obj_align(btn_feed, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *feed_lbl = lv_label_create(btn_feed);
    lv_label_set_text(feed_lbl, "Nourrir");
    lv_obj_center(feed_lbl);
    lv_obj_add_event_cb(btn_feed, feed_btn_event_cb, LV_EVENT_CLICKED, NULL);

    // Heater toggle button
    lv_obj_t *btn_heat = lv_btn_create(screen_game);
    lv_obj_set_size(btn_heat, 120, 40);
    lv_obj_align(btn_heat, LV_ALIGN_BOTTOM_LEFT, 120, -10);
    lv_obj_t *heat_lbl = lv_label_create(btn_heat);
    lv_label_set_text(heat_lbl, "Chauffage: OFF");
    lv_obj_center(heat_lbl);
    lv_obj_add_event_cb(btn_heat, heat_btn_event_cb, LV_EVENT_CLICKED, heat_lbl);

    // Pause button
    lv_obj_t *btn_pause = lv_btn_create(screen_game);
    lv_obj_set_size(btn_pause, 80, 35);
    lv_obj_align(btn_pause, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_t *pause_lbl = lv_label_create(btn_pause);
    lv_label_set_text(pause_lbl, "Pause");
    lv_obj_center(pause_lbl);
    lv_obj_add_event_cb(btn_pause, pause_btn_event_cb, LV_EVENT_CLICKED, pause_lbl);

    // Pet representation (simple square)
    pet_obj = lv_obj_create(screen_game);
    lv_obj_set_size(pet_obj, 30, 30);
    lv_obj_set_style_bg_color(pet_obj, lv_color_make(0x00, 0xC8, 0x00), 0);
    lv_obj_align(pet_obj, LV_ALIGN_CENTER, 0, 0);

    // Instrumentation label (FPS and memory)
    label_perf = lv_label_create(screen_game);
    lv_obj_align(label_perf, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    lv_label_set_text(label_perf, "FPS: --\nRAM libre: ---- KB");

    // Initially load the home screen
    lv_scr_load(screen_home);
}

// Post a status update asynchronously.  A copy of the input string
// is allocated and passed to the update_status_label_async()
// function via lv_async_call().  The callback will update the label
// and free the string afterwards.
void display_update_status_async(const char *status)
{
    if (!label_status) {
        return;
    }
    // Make a copy of the status text because the string may go out of scope
    char *copy = strdup(status);
    if (copy) {
        lv_async_call(update_status_label_async, copy);
    }
}

// The LVGL asynchronous callback that updates the status label.  The
// parameter is assumed to be a char* allocated via strdup().  It is
// freed after the label has been updated.
static void update_status_label_async(void *param)
{
    char *text = (char *)param;
    if (label_status && text) {
        lv_label_set_text(label_status, text);
    }
    free(text);
}

// Button event callbacks
static void start_btn_event_cb(lv_event_t *e)
{
    // Switch to the game screen and signal that the game is started
    lv_scr_load(screen_game);
    game_started = true;
}

static void feed_btn_event_cb(lv_event_t *e)
{
    game_post_event(GAME_EVENT_FEED);
}

static void heat_btn_event_cb(lv_event_t *e)
{
    // The user data points to the label inside the button.  We
    // toggle the heater state and update the label accordingly.
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    static bool heater_on = false;
    heater_on = !heater_on;
    game_post_event(heater_on ? GAME_EVENT_HEAT_ON : GAME_EVENT_HEAT_OFF);
    // Update the button label
    if (lbl) {
        lv_label_set_text(lbl, heater_on ? "Chauffage: ON" : "Chauffage: OFF");
    }
}

static void pause_btn_event_cb(lv_event_t *e)
{
    // user data points to the label inside the button.  Toggle the paused
    // state and send appropriate event to the game logic
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    static bool paused = false;
    paused = !paused;
    game_post_event(paused ? GAME_EVENT_PAUSE : GAME_EVENT_RESUME);
    if (lbl) {
        lv_label_set_text(lbl, paused ? "Reprendre" : "Pause");
    }
}

// The main display task.  This function runs on its own FreeRTOS
// task and continuously calls the LVGL handler, processes
// animation messages and updates the instrumentation once per
// second.
void display_task(void *arg)
{
    (void)arg;
    // Local variables for instrumentation
    TickType_t last_report = xTaskGetTickCount();
    uint32_t last_frame_count = 0;
    TickType_t delay = pdMS_TO_TICKS(5);

    while (1) {
        lv_timer_handler();

        // Every second update the performance label
        TickType_t now = xTaskGetTickCount();
        if (now - last_report >= pdMS_TO_TICKS(1000)) {
            uint32_t frames = frame_count;
            uint32_t fps = frames - last_frame_count;
            last_frame_count = frames;
            size_t free_heap = esp_get_free_heap_size();
            if (label_perf) {
                char buf[64];
                snprintf(buf, sizeof(buf), "FPS: %u\nRAM libre: %u KB", fps, (unsigned)(free_heap / 1024));
                lv_label_set_text(label_perf, buf);
            }
            // Optionally gather run time stats for each task
            char runtime_buf[256];
            memset(runtime_buf, 0, sizeof(runtime_buf));
            vTaskGetRunTimeStats(runtime_buf);
            ESP_LOGI(TAG, "Run time stats:\n%s", runtime_buf);
            last_report = now;
        }
        vTaskDelay(delay);
    }
}