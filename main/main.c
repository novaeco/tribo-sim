// Main application entry point for the Reptile Simulation game
//
// This file orchestrates the initialization of all subsystems (storage,
// display, input, game logic, animation, audio and OTA) and then spawns
// the appropriate FreeRTOS tasks.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "sim_display.h"
#include "input.h"
#include "game.h"
#include "anim.h"
#include "storage.h"
#include "audio.h"
#include "ota.h"

static const char *TAG = "MAIN";

// Flag indicating that the user has pressed the "Commencer" button.
// Updated from the LVGL event callback in sim_display.c.
// Defined in game.c and declared in game.h

void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "   Tribo-Sim Reptile Simulator   ");
    ESP_LOGI(TAG, "   Version: %s", ota_get_version());
    ESP_LOGI(TAG, "   Build: %s", ota_get_build_date());
    ESP_LOGI(TAG, "=================================");

    // Initialize OTA subsystem (validates firmware on first boot)
    ota_init();

    // Initialize persistent storage (mount SPIFFS or SD as configured)
    ESP_LOGI(TAG, "Initializing storage...");
    storage_init();

    // Bring up the LCD panel and start LVGL
    ESP_LOGI(TAG, "Initializing display...");
    display_init_panel();
    lvgl_start();

    // Initialize touch controller
    ESP_LOGI(TAG, "Initializing touch input...");
    touch_init();

    // Create UI
    ESP_LOGI(TAG, "Creating UI...");
    create_ui();

    // Initialize game logic
    ESP_LOGI(TAG, "Initializing game...");
    game_init();

    // Create FreeRTOS tasks distributed across both cores
    ESP_LOGI(TAG, "Starting tasks...");

    // Core 0: Display and Input (UI-related)
    xTaskCreatePinnedToCore(display_task, "Display", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(sensor_task, "Input", 4096, NULL, 4, NULL, 0);

    // Core 1: Game logic, Animation, and Audio
    xTaskCreatePinnedToCore(game_task, "Game", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(anim_task, "Anim", 3072, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(audio_task, "Audio", 4096, NULL, 1, NULL, 1);

    ESP_LOGI(TAG, "All systems initialized. Waiting for user to start game...");

    // app_main() returns, FreeRTOS continues scheduling tasks
}
