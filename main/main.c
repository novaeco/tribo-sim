/**
 * @file main.c
 * @brief REPTILE SIM ULTIMATE - Main Entry Point
 * @version 3.0 (3-TIER ARCHITECTURE)
 *
 * TIER 1: BSP (esp32p4_reptile_bsp) - Hardware drivers
 * TIER 2: CORE (reptile_core) - C++ Simulation engine
 * TIER 3: APP (main) - Integration & UI
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_spiffs.h"
#include <stdbool.h>
#include <string.h>

// TIER 1: BSP
#include "bsp_reptile.h"

// TIER 2: Simulation Core (C interface)
#include "reptile_engine_c.h"

// LVGL
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "REPTILE_SIM";

// ====================================================================================
// GLOBAL HANDLES
// ====================================================================================

static lv_display_t *g_lvgl_display = NULL;
static lv_indev_t *g_lvgl_indev = NULL;
static lv_obj_t *g_main_screen = NULL;

// UI Elements
static lv_obj_t *g_label_status = NULL;
static lv_obj_t *g_label_time = NULL;
static lv_obj_t *g_label_stats = NULL;

// Screens
static lv_obj_t *g_screen_dashboard = NULL;
static lv_obj_t *g_screen_terrarium = NULL;
static lv_obj_t *g_screen_reptiles = NULL;

// Control buttons
static lv_obj_t *g_btn_heater = NULL;
static lv_obj_t *g_btn_light = NULL;
static lv_obj_t *g_btn_mister = NULL;
static lv_obj_t *g_btn_feed = NULL;
static lv_obj_t *g_btn_clean = NULL;

// Status labels for terrarium screen
static lv_obj_t *g_label_temp = NULL;
static lv_obj_t *g_label_humidity = NULL;
static lv_obj_t *g_label_waste = NULL;

// Alert message box
static lv_obj_t *g_alert_msgbox = NULL;

// Multi-terrarium management
static uint32_t g_selected_terrarium_id = 1;

// Multi-reptile management
static uint32_t g_selected_reptile_id = 1;

// ====================================================================================
// FORWARD DECLARATIONS
// ====================================================================================

typedef enum {
    ALERT_INFO,
    ALERT_WARNING,
    ALERT_CRITICAL
} alert_type_t;

static void save_game_state(void);
static void load_game_state(void);
static void show_alert(alert_type_t type, const char *title, const char *message);

static void lvgl_self_test_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    static bool toggle = false;

    if (!g_main_screen || !g_label_status) {
        return;
    }

    lv_color_t bg = toggle ? lv_color_hex(0x8B0000) : lv_color_hex(0x0D1F0D);
    lv_obj_set_style_bg_color(g_main_screen, bg, 0);
    lv_label_set_text(g_label_status, toggle ? "LVGL TEST: RED" : "REPTILE SIM ULTIMATE v3.0");
    toggle = !toggle;
}

// ====================================================================================
// RTOS TASKS
// ====================================================================================

/**
 * @brief Simulation Task (1Hz)
 * Runs the C++ simulation engine
 */
static void simulation_task(void *arg)
{
    ESP_LOGI(TAG, "Simulation task started");

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000); // 1 second

    while (1) {
        // Call C++ engine tick (delta_time = 1.0 seconds)
        reptile_engine_tick(1.0f);

        // Wait for next tick
        vTaskDelayUntil(&last_wake, period);
    }
}

/**
 * @brief UI Update Task (30Hz)
 * Updates LVGL widgets with simulation data
 */
static void ui_update_task(void *arg)
{
    ESP_LOGI(TAG, "UI update task started");

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(33); // ~30 FPS

    while (1) {
        uint32_t day = reptile_engine_get_day();
        float hours = reptile_engine_get_time_hours();
        int reptile_count = reptile_engine_get_reptile_count();
        int terrarium_count = reptile_engine_get_terrarium_count();

        if (g_label_time && g_label_stats) {

            // Update time label
            char time_buf[64];
            snprintf(time_buf, sizeof(time_buf), "Day %lu - %02d:%02d",
                     day, (int)hours, (int)((hours - (int)hours) * 60.0f));

            // Update stats label
            char stats_buf[128];
            snprintf(stats_buf, sizeof(stats_buf),
                     "Animals: %d | Terrariums: %d",
                     reptile_count, terrarium_count);

            lvgl_port_lock(0);
            lv_label_set_text(g_label_time, time_buf);
            lv_label_set_text(g_label_stats, stats_buf);
            lvgl_port_unlock();
        }

        // Update terrarium screen data (if labels exist)
        if (g_label_temp && g_label_humidity && g_label_waste) {
            // Get real-time terrarium data from engine (terrarium ID 1)
            float temp = reptile_engine_get_terrarium_temp(g_selected_terrarium_id);
            float humidity = reptile_engine_get_terrarium_humidity(g_selected_terrarium_id);
            float waste = reptile_engine_get_terrarium_waste(g_selected_terrarium_id);

            // Format temperature with icon
            char temp_buf[64];
            snprintf(temp_buf, sizeof(temp_buf), LV_SYMBOL_WARNING " Temp: %.1f°C", temp);

            // Format humidity with icon
            char humidity_buf[64];
            snprintf(humidity_buf, sizeof(humidity_buf), LV_SYMBOL_REFRESH " Humidity: %.1f%%", humidity);

            // Format waste with icon
            char waste_buf[64];
            snprintf(waste_buf, sizeof(waste_buf), LV_SYMBOL_TRASH " Waste: %.1f%%", waste);

            lvgl_port_lock(0);
            lv_label_set_text(g_label_temp, temp_buf);
            lv_label_set_text(g_label_humidity, humidity_buf);
            lv_label_set_text(g_label_waste, waste_buf);
            lvgl_port_unlock();

            // Check for critical conditions (alert every 30 seconds to avoid spam)
            static uint32_t last_alert_tick = 0;
            uint32_t now_tick = xTaskGetTickCount();
            if ((now_tick - last_alert_tick) > pdMS_TO_TICKS(30000)) { // 30 seconds
                // Temperature alerts
                if (temp > 38.0f) {
                    lvgl_port_lock(0);
                    show_alert(ALERT_CRITICAL, "DANGER!", "Temperature too high!\nRisk of overheating.");
                    lvgl_port_unlock();
                    last_alert_tick = now_tick;
                }
                else if (temp < 20.0f) {
                    lvgl_port_lock(0);
                    show_alert(ALERT_WARNING, "Warning", "Temperature too low!\nTurn on heater.");
                    lvgl_port_unlock();
                    last_alert_tick = now_tick;
                }
                // Waste level alerts
                else if (waste > 80.0f) {
                    lvgl_port_lock(0);
                    show_alert(ALERT_WARNING, "Sanitation Alert", "Waste level critical!\nClean terrarium now.");
                    lvgl_port_unlock();
                    last_alert_tick = now_tick;
                }
                // Reptile health alerts
                else if (reptile_count > 0) {
                    float stress = reptile_engine_get_reptile_stress(1);
                    bool hungry = reptile_engine_is_reptile_hungry(1);
                    bool healthy = reptile_engine_is_reptile_healthy(1);

                    if (!healthy) {
                        lvgl_port_lock(0);
                        show_alert(ALERT_CRITICAL, "HEALTH CRISIS!", "Animal is sick!\nCheck conditions immediately.");
                        lvgl_port_unlock();
                        last_alert_tick = now_tick;
                    }
                    else if (stress > 80.0f) {
                        lvgl_port_lock(0);
                        show_alert(ALERT_WARNING, "Stress Alert", "Animal is very stressed!\nImprove habitat conditions.");
                        lvgl_port_unlock();
                        last_alert_tick = now_tick;
                    }
                    else if (hungry) {
                        lvgl_port_lock(0);
                        show_alert(ALERT_INFO, "Feeding Time", "Animal is hungry.\nFeed your reptile.");
                        lvgl_port_unlock();
                        last_alert_tick = now_tick;
                    }
                }
            }
        }

        vTaskDelayUntil(&last_wake, period);
    }
}

/**
 * @brief LVGL Handler Task (fallback)
 * Ensures LVGL timers/flush run even if the port task is not started.
 */
static void lvgl_fallback_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL fallback handler task started");

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(5);

    while (1) {
        lvgl_port_lock(0);
        lv_tick_inc(5);
        lv_timer_handler();
        lvgl_port_unlock();

        vTaskDelayUntil(&last_wake, period);
    }
}

/**
 * @brief Auto-save task (every 5 minutes)
 */
static void autosave_task(void *arg)
{
    ESP_LOGI(TAG, "Auto-save task started (5-minute intervals)");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(300000)); // 5 minutes
        save_game_state();
    }
}

// ====================================================================================
// SAVE/LOAD SYSTEM (SPIFFS - Complete State)
// ====================================================================================

static void save_game_state(void)
{
    ESP_LOGI(TAG, "Saving complete game state to SPIFFS...");

    bool success = reptile_engine_save_game("/spiffs/savegame.txt");
    if (success) {
        ESP_LOGI(TAG, "Game saved successfully (reptiles, terrariums, economy)");
    } else {
        ESP_LOGW(TAG, "Failed to save game state");
    }
}

static void load_game_state(void)
{
    ESP_LOGI(TAG, "Loading complete game state from SPIFFS...");

    bool success = reptile_engine_load_game("/spiffs/savegame.txt");
    if (success) {
        ESP_LOGI(TAG, "Game loaded successfully");
    } else {
        ESP_LOGI(TAG, "No saved game found (first run)");
    }
}

// ====================================================================================
// ALERT SYSTEM
// ====================================================================================

static void alert_msgbox_close_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_msgbox_close(g_alert_msgbox);
        g_alert_msgbox = NULL;
    }
}

static void show_alert(alert_type_t type, const char *title, const char *message)
{
    // Close previous alert if exists
    if (g_alert_msgbox) {
        lv_msgbox_close(g_alert_msgbox);
    }

    // Choose color based on alert type
    lv_color_t title_color;
    switch (type) {
        case ALERT_CRITICAL:
            title_color = lv_color_hex(0xFF0000); // Red
            break;
        case ALERT_WARNING:
            title_color = lv_color_hex(0xFFA500); // Orange
            break;
        case ALERT_INFO:
        default:
            title_color = lv_color_hex(0x00FF00); // Green
            break;
    }

    // Create message box (LVGL 9 API)
    g_alert_msgbox = lv_msgbox_create(NULL);
    lv_obj_set_style_bg_color(g_alert_msgbox, lv_color_hex(0x1F1B24), 0);

    // Style title
    lv_obj_t *title_label = lv_msgbox_add_title(g_alert_msgbox, title);
    lv_obj_set_style_text_color(title_label, title_color, 0);

    // Style content
    lv_obj_t *content_label = lv_msgbox_add_text(g_alert_msgbox, message);
    lv_obj_set_style_text_color(content_label, lv_color_hex(0xCCCCCC), 0);

    // Add close button
    lv_obj_t *close_btn = lv_msgbox_add_footer_button(g_alert_msgbox, "OK");
    lv_obj_add_event_cb(close_btn, alert_msgbox_close_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Alert shown: [%s] %s", title, message);
}

// ====================================================================================
// UI CALLBACKS
// ====================================================================================

static void btn_dashboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Dashboard button clicked");
        lv_scr_load(g_screen_dashboard);
    }
}

static void btn_terrarium_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Terrarium button clicked");
        lv_scr_load(g_screen_terrarium);
    }
}

static void btn_reptiles_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Reptiles button clicked");
        lv_scr_load(g_screen_reptiles);
    }
}

static void btn_heater_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Get current state and toggle
        bool current_state = reptile_engine_get_heater_state(g_selected_terrarium_id); // Terrarium ID 1
        bool new_state = !current_state;
        reptile_engine_set_heater(g_selected_terrarium_id, new_state);

        ESP_LOGI(TAG, "Heater toggled: %s", new_state ? "ON" : "OFF");

        // Update button label
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, new_state ? LV_SYMBOL_POWER " Heater ON" : LV_SYMBOL_POWER " Heater OFF");
    }
}

static void btn_light_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Get current state and toggle
        bool current_state = reptile_engine_get_light_state(g_selected_terrarium_id); // Terrarium ID 1
        bool new_state = !current_state;
        reptile_engine_set_light(g_selected_terrarium_id, new_state);

        ESP_LOGI(TAG, "Light toggled: %s", new_state ? "ON" : "OFF");

        // Update button label
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, new_state ? LV_SYMBOL_IMAGE " Light ON" : LV_SYMBOL_IMAGE " Light OFF");
    }
}

static void btn_mister_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Get current state and toggle
        bool current_state = reptile_engine_get_mister_state(g_selected_terrarium_id); // Terrarium ID 1
        bool new_state = !current_state;
        reptile_engine_set_mister(g_selected_terrarium_id, new_state);

        ESP_LOGI(TAG, "Mister toggled: %s", new_state ? "ON" : "OFF");

        // Update button label
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, new_state ? LV_SYMBOL_REFRESH " Mister ON" : LV_SYMBOL_REFRESH " Mister OFF");
    }
}

static void btn_feed_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reptile_engine_feed_animal(g_selected_reptile_id); // Reptile ID 1
        ESP_LOGI(TAG, "Fed animal ID 1 (+$2 food cost)");
    }
}

static void btn_clean_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reptile_engine_clean_terrarium(g_selected_terrarium_id);
        ESP_LOGI(TAG, "Cleaned terrarium ID %lu (waste/bacteria reduced)", g_selected_terrarium_id);
    }
}

static void btn_add_terrarium_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        uint32_t new_id = reptile_engine_add_terrarium(120.0f, 60.0f, 60.0f);
        ESP_LOGI(TAG, "Added terrarium ID %lu (120x60x60 cm)", new_id);

        lvgl_port_lock(0);
        show_alert(ALERT_INFO, "Success", "New terrarium added!");
        lvgl_port_unlock();
    }
}

static void btn_add_reptile_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        uint32_t new_id = reptile_engine_add_reptile("New Reptile", "Pogona vitticeps");
        ESP_LOGI(TAG, "Added reptile ID %lu (Pogona vitticeps)", new_id);

        lvgl_port_lock(0);
        show_alert(ALERT_INFO, "Success", "New reptile added!");
        lvgl_port_unlock();
    }
}

static void btn_terrarium_prev_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (g_selected_terrarium_id > 1) {
            g_selected_terrarium_id--;
            ESP_LOGI(TAG, "Selected terrarium ID %lu", g_selected_terrarium_id);
        }
    }
}

static void btn_terrarium_next_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        int count = reptile_engine_get_terrarium_count();
        if (g_selected_terrarium_id < (uint32_t)count) {
            g_selected_terrarium_id++;
            ESP_LOGI(TAG, "Selected terrarium ID %lu", g_selected_terrarium_id);
        }
    }
}

static void btn_reptile_prev_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (g_selected_reptile_id > 1) {
            g_selected_reptile_id--;
            ESP_LOGI(TAG, "Selected reptile ID %lu", g_selected_reptile_id);
        }
    }
}

static void btn_reptile_next_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        int count = reptile_engine_get_reptile_count();
        if (g_selected_reptile_id < (uint32_t)count) {
            g_selected_reptile_id++;
            ESP_LOGI(TAG, "Selected reptile ID %lu", g_selected_reptile_id);
        }
    }
}

static void btn_save_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Save button clicked");
        save_game_state();
    }
}

// ====================================================================================
// UI CREATION
// ====================================================================================

static void create_dashboard_screen(void)
{
    ESP_LOGI(TAG, "Creating dashboard screen...");

    g_screen_dashboard = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen_dashboard, lv_color_hex(0x0D1F0D), 0);

    // Title
    g_label_status = lv_label_create(g_screen_dashboard);
    lv_label_set_text(g_label_status, "REPTILE SIM v3.0");
    lv_obj_set_style_text_color(g_label_status, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_text_font(g_label_status, &lv_font_montserrat_24, 0);
    lv_obj_align(g_label_status, LV_ALIGN_TOP_MID, 0, 10);

    // Time label
    g_label_time = lv_label_create(g_screen_dashboard);
    lv_label_set_text(g_label_time, "Day 1 - 12:00");
    lv_obj_set_style_text_color(g_label_time, lv_color_hex(0xF1F8E9), 0);
    lv_obj_set_style_text_font(g_label_time, &lv_font_montserrat_20, 0);
    lv_obj_align(g_label_time, LV_ALIGN_TOP_MID, 0, 50);

    // Stats label
    g_label_stats = lv_label_create(g_screen_dashboard);
    lv_label_set_text(g_label_stats, "Loading...");
    lv_obj_set_style_text_color(g_label_stats, lv_color_hex(0xA5D6A7), 0);
    lv_obj_set_style_text_font(g_label_stats, &lv_font_montserrat_18, 0);
    lv_obj_align(g_label_stats, LV_ALIGN_TOP_MID, 0, 90);

    // Navigation buttons
    lv_obj_t *btn_terrarium_nav = lv_btn_create(g_screen_dashboard);
    lv_obj_set_size(btn_terrarium_nav, 200, 60);
    lv_obj_align(btn_terrarium_nav, LV_ALIGN_CENTER, -120, 0);
    lv_obj_add_event_cb(btn_terrarium_nav, btn_terrarium_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label1 = lv_label_create(btn_terrarium_nav);
    lv_label_set_text(label1, LV_SYMBOL_HOME " Terrarium");
    lv_obj_center(label1);

    lv_obj_t *btn_reptiles_nav = lv_btn_create(g_screen_dashboard);
    lv_obj_set_size(btn_reptiles_nav, 200, 60);
    lv_obj_align(btn_reptiles_nav, LV_ALIGN_CENTER, 120, 0);
    lv_obj_add_event_cb(btn_reptiles_nav, btn_reptiles_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label2 = lv_label_create(btn_reptiles_nav);
    lv_label_set_text(label2, LV_SYMBOL_LIST " Reptiles");
    lv_obj_center(label2);

    // Save button
    lv_obj_t *btn_save = lv_btn_create(g_screen_dashboard);
    lv_obj_set_size(btn_save, 180, 50);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_add_event_cb(btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_save = lv_label_create(btn_save);
    lv_label_set_text(label_save, LV_SYMBOL_SAVE " Save Game");
    lv_obj_center(label_save);

    // Status indicator
    lv_obj_t *label_ready = lv_label_create(g_screen_dashboard);
    lv_label_set_text(label_ready, LV_SYMBOL_OK " System Ready");
    lv_obj_set_style_text_color(label_ready, lv_color_hex(0x8BC34A), 0);
    lv_obj_align(label_ready, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static void create_terrarium_screen(void)
{
    ESP_LOGI(TAG, "Creating terrarium control screen...");

    g_screen_terrarium = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen_terrarium, lv_color_hex(0x1A1A2E), 0);

    // Title
    lv_obj_t *title = lv_label_create(g_screen_terrarium);
    lv_label_set_text(title, LV_SYMBOL_HOME " Terrarium Control");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Navigation: Previous button
    lv_obj_t *btn_prev = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(btn_prev, 80, 40);
    lv_obj_align(btn_prev, LV_ALIGN_TOP_LEFT, 10, 45);
    lv_obj_add_event_cb(btn_prev, btn_terrarium_prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_prev = lv_label_create(btn_prev);
    lv_label_set_text(label_prev, LV_SYMBOL_LEFT);
    lv_obj_center(label_prev);

    // Terrarium selector label (will be updated dynamically)
    lv_obj_t *label_selector = lv_label_create(g_screen_terrarium);
    lv_label_set_text(label_selector, "Terrarium 1/1");
    lv_obj_set_style_text_color(label_selector, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_selector, LV_ALIGN_TOP_MID, 0, 50);

    // Navigation: Next button
    lv_obj_t *btn_next = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(btn_next, 80, 40);
    lv_obj_align(btn_next, LV_ALIGN_TOP_RIGHT, -10, 45);
    lv_obj_add_event_cb(btn_next, btn_terrarium_next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_next = lv_label_create(btn_next);
    lv_label_set_text(label_next, LV_SYMBOL_RIGHT);
    lv_obj_center(label_next);

    // Add Terrarium button
    lv_obj_t *btn_add = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(btn_add, 150, 40);
    lv_obj_align(btn_add, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_add_event_cb(btn_add, btn_add_terrarium_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_add = lv_label_create(btn_add);
    lv_label_set_text(label_add, LV_SYMBOL_PLUS " Add");
    lv_obj_center(label_add);

    // Status displays
    g_label_temp = lv_label_create(g_screen_terrarium);
    lv_label_set_text(g_label_temp, LV_SYMBOL_WARNING " Temp: --°C");
    lv_obj_set_style_text_color(g_label_temp, lv_color_hex(0xFFEB3B), 0);
    lv_obj_align(g_label_temp, LV_ALIGN_TOP_LEFT, 20, 145);

    g_label_humidity = lv_label_create(g_screen_terrarium);
    lv_label_set_text(g_label_humidity, LV_SYMBOL_REFRESH " Humidity: --%");
    lv_obj_set_style_text_color(g_label_humidity, lv_color_hex(0x03A9F4), 0);
    lv_obj_align(g_label_humidity, LV_ALIGN_TOP_LEFT, 20, 175);

    g_label_waste = lv_label_create(g_screen_terrarium);
    lv_label_set_text(g_label_waste, LV_SYMBOL_TRASH " Waste: --%");
    lv_obj_set_style_text_color(g_label_waste, lv_color_hex(0xFF9800), 0);
    lv_obj_align(g_label_waste, LV_ALIGN_TOP_LEFT, 20, 205);

    // Control buttons
    g_btn_heater = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(g_btn_heater, 180, 50);
    lv_obj_align(g_btn_heater, LV_ALIGN_CENTER, -100, -50);
    lv_obj_add_event_cb(g_btn_heater, btn_heater_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_h = lv_label_create(g_btn_heater);
    lv_label_set_text(label_h, LV_SYMBOL_POWER " Heater ON");
    lv_obj_center(label_h);

    g_btn_light = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(g_btn_light, 180, 50);
    lv_obj_align(g_btn_light, LV_ALIGN_CENTER, 100, -50);
    lv_obj_add_event_cb(g_btn_light, btn_light_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_l = lv_label_create(g_btn_light);
    lv_label_set_text(label_l, LV_SYMBOL_IMAGE " Light ON");
    lv_obj_center(label_l);

    g_btn_mister = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(g_btn_mister, 180, 50);
    lv_obj_align(g_btn_mister, LV_ALIGN_CENTER, -100, 20);
    lv_obj_add_event_cb(g_btn_mister, btn_mister_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_m = lv_label_create(g_btn_mister);
    lv_label_set_text(label_m, LV_SYMBOL_REFRESH " Mister OFF");
    lv_obj_center(label_m);

    g_btn_clean = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(g_btn_clean, 180, 50);
    lv_obj_align(g_btn_clean, LV_ALIGN_CENTER, 100, 20);
    lv_obj_add_event_cb(g_btn_clean, btn_clean_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_c = lv_label_create(g_btn_clean);
    lv_label_set_text(label_c, LV_SYMBOL_TRASH " Clean");
    lv_obj_center(label_c);

    // Back button
    lv_obj_t *btn_back = lv_btn_create(g_screen_terrarium);
    lv_obj_set_size(btn_back, 150, 50);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_back, btn_dashboard_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(label_back);
}

static void create_reptiles_screen(void)
{
    ESP_LOGI(TAG, "Creating reptiles screen...");

    g_screen_reptiles = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen_reptiles, lv_color_hex(0x1F1B24), 0);

    // Title
    lv_obj_t *title = lv_label_create(g_screen_reptiles);
    lv_label_set_text(title, LV_SYMBOL_LIST " Reptile Status");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE91E63), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Navigation: Previous button
    lv_obj_t *btn_prev = lv_btn_create(g_screen_reptiles);
    lv_obj_set_size(btn_prev, 80, 40);
    lv_obj_align(btn_prev, LV_ALIGN_TOP_LEFT, 10, 45);
    lv_obj_add_event_cb(btn_prev, btn_reptile_prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_prev = lv_label_create(btn_prev);
    lv_label_set_text(label_prev, LV_SYMBOL_LEFT);
    lv_obj_center(label_prev);

    // Reptile selector label (will be updated dynamically)
    lv_obj_t *label_selector = lv_label_create(g_screen_reptiles);
    lv_label_set_text(label_selector, "Reptile 1/1");
    lv_obj_set_style_text_color(label_selector, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_selector, LV_ALIGN_TOP_MID, 0, 50);

    // Navigation: Next button
    lv_obj_t *btn_next = lv_btn_create(g_screen_reptiles);
    lv_obj_set_size(btn_next, 80, 40);
    lv_obj_align(btn_next, LV_ALIGN_TOP_RIGHT, -10, 45);
    lv_obj_add_event_cb(btn_next, btn_reptile_next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_next = lv_label_create(btn_next);
    lv_label_set_text(label_next, LV_SYMBOL_RIGHT);
    lv_obj_center(label_next);

    // Add Reptile button
    lv_obj_t *btn_add = lv_btn_create(g_screen_reptiles);
    lv_obj_set_size(btn_add, 150, 40);
    lv_obj_align(btn_add, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_add_event_cb(btn_add, btn_add_reptile_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_add = lv_label_create(btn_add);
    lv_label_set_text(label_add, LV_SYMBOL_PLUS " Add");
    lv_obj_center(label_add);

    // Reptile info (placeholder - will be updated dynamically)
    lv_obj_t *info = lv_label_create(g_screen_reptiles);
    lv_label_set_text(info, "Loading reptile data...");
    lv_obj_set_style_text_color(info, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 10);

    // Feed button
    g_btn_feed = lv_btn_create(g_screen_reptiles);
    lv_obj_set_size(g_btn_feed, 180, 50);
    lv_obj_align(g_btn_feed, LV_ALIGN_CENTER, 0, 80);
    lv_obj_add_event_cb(g_btn_feed, btn_feed_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_f = lv_label_create(g_btn_feed);
    lv_label_set_text(label_f, LV_SYMBOL_IMAGE " Feed Animal");
    lv_obj_center(label_f);

    // Back button
    lv_obj_t *btn_back = lv_btn_create(g_screen_reptiles);
    lv_obj_set_size(btn_back, 150, 50);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_back, btn_dashboard_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(label_back);
}

static void create_ui(void)
{
    ESP_LOGI(TAG, "Creating multi-screen UI...");

    // Create all screens
    create_dashboard_screen();
    create_terrarium_screen();
    create_reptiles_screen();

    // Load dashboard as initial screen
    lv_scr_load(g_screen_dashboard);

    // LVGL self-test timer (toggles background)
    lv_timer_create(lvgl_self_test_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "UI created with 3 screens: Dashboard, Terrarium, Reptiles");
}

// ====================================================================================
// MAIN
// ====================================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, "  REPTILE SIM ULTIMATE v3.0");
    ESP_LOGI(TAG, "  3-TIER ARCHITECTURE");
    ESP_LOGI(TAG, "===================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ====================================================================================
    // Initialize LVGL Port
    // ====================================================================================

    ESP_LOGI(TAG, "Initializing LVGL port...");

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // ====================================================================================
    // TIER 1: Initialize BSP (Hardware)
    // ====================================================================================

    ESP_LOGI(TAG, "[TIER 1] Initializing BSP...");

    // Display
    ESP_ERROR_CHECK(bsp_display_init(&g_lvgl_display));

    // Touch
    ESP_ERROR_CHECK(bsp_touch_init(&g_lvgl_indev, g_lvgl_display));

    // SD Card (optional)
    bsp_sdcard_mount(); // Non-critical

    // SPIFFS (for game saves)
    ESP_LOGI(TAG, "Mounting SPIFFS...");
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret_spiffs = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret_spiffs != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret_spiffs));
    } else {
        size_t total = 0, used = 0;
        ret_spiffs = esp_spiffs_info("storage", &total, &used);
        if (ret_spiffs == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
        }
    }

    // ====================================================================================
    // TIER 2: Initialize Simulation Core (C++)
    // ====================================================================================

    ESP_LOGI(TAG, "[TIER 2] Initializing Simulation Core...");
    reptile_engine_init();

    // Load saved game state (if exists)
    load_game_state();

    // ====================================================================================
    // TIER 3: Create UI
    // ====================================================================================

    ESP_LOGI(TAG, "[TIER 3] Creating UI...");
    lvgl_port_lock(0);
    create_ui();
    lvgl_port_unlock();

    // ====================================================================================
    // Create RTOS Tasks
    // ====================================================================================

    ESP_LOGI(TAG, "Creating tasks...");

    xTaskCreate(simulation_task, "sim_task", 8192, NULL, 5, NULL);
    xTaskCreate(ui_update_task, "ui_task", 4096, NULL, 4, NULL);
    xTaskCreate(lvgl_fallback_task, "lvgl_fallback", 4096, NULL,
                CONFIG_APP_LVGL_TASK_PRIORITY, NULL);
    xTaskCreate(autosave_task, "autosave", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, "  SYSTEM READY");
    ESP_LOGI(TAG, "  - 14 simulation engines active");
    ESP_LOGI(TAG, "  - Interactive touch UI enabled");
    ESP_LOGI(TAG, "  - Auto-save every 5 minutes");
    ESP_LOGI(TAG, "===================================");

    // Main loop (idle)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
