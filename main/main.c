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
        if (g_label_time && g_label_stats) {
            // Get data from C++ engine
            uint32_t day = reptile_engine_get_day();
            float hours = reptile_engine_get_time_hours();
            int reptile_count = reptile_engine_get_reptile_count();
            int terrarium_count = reptile_engine_get_terrarium_count();

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

// ====================================================================================
// UI CREATION
// ====================================================================================

static void create_ui(void)
{
    ESP_LOGI(TAG, "Creating UI...");

    // Main screen
    g_main_screen = lv_scr_act();
    lv_obj_set_style_bg_color(g_main_screen, lv_color_hex(0x0D1F0D), 0);

    // Status label (top)
    g_label_status = lv_label_create(g_main_screen);
    lv_label_set_text(g_label_status, "REPTILE SIM ULTIMATE v3.0");
    lv_obj_set_style_text_color(g_label_status, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_text_font(g_label_status, &lv_font_montserrat_24, 0);
    lv_obj_align(g_label_status, LV_ALIGN_TOP_MID, 0, 20);

    // Time label
    g_label_time = lv_label_create(g_main_screen);
    lv_label_set_text(g_label_time, "Day 1 - 12:00");
    lv_obj_set_style_text_color(g_label_time, lv_color_hex(0xF1F8E9), 0);
    lv_obj_set_style_text_font(g_label_time, &lv_font_montserrat_20, 0);
    lv_obj_align(g_label_time, LV_ALIGN_CENTER, 0, -50);

    // Stats label
    g_label_stats = lv_label_create(g_main_screen);
    lv_label_set_text(g_label_stats, "Loading...");
    lv_obj_set_style_text_color(g_label_stats, lv_color_hex(0xA5D6A7), 0);
    lv_obj_set_style_text_font(g_label_stats, &lv_font_montserrat_18, 0);
    lv_obj_align(g_label_stats, LV_ALIGN_CENTER, 0, 0);

    // System ready indicator
    lv_obj_t *label_ready = lv_label_create(g_main_screen);
    lv_label_set_text(label_ready, LV_SYMBOL_OK " System Ready");
    lv_obj_set_style_text_color(label_ready, lv_color_hex(0x66BB6A), 0);
    lv_obj_align(label_ready, LV_ALIGN_BOTTOM_MID, 0, -50);

    lv_timer_create(lvgl_self_test_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "UI created successfully");
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

    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, "  SYSTEM READY");
    ESP_LOGI(TAG, "===================================");

    // Main loop (idle)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
