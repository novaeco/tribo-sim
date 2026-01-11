// Main application entry point for the Reptile Simulation game
//
// This file orchestrates the initialization of all subsystems (storage,
// display, input, game logic and animation) and then spawns the
// appropriate FreeRTOS tasks.  It follows the architecture described in
// the project overview: separate tasks for display/UI, input polling,
// game logic and animations.  Each component exposes its own
// initialization function via a header in the corresponding component
// directory.  The goal of this file is to glue everything together.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sim_display.h"
#include "input.h"
#include "game.h"
#include "anim.h"
#include "storage.h"

// Flag indicating that the user has pressed the "Commencer" button on
// the UI.  Until this flag is set to true, the game and animation
// tasks will sit idle in a waiting loop.  It is updated from the
// LVGL event callback in display.c.
bool game_started = false;

void app_main(void)
{
    // Initialize persistent storage (mount SPIFFS or SD as configured)
    storage_init();

    // Bring up the LCD panel and start LVGL.  These functions will
    // configure the MIPI‑DSI bus, allocate the frame buffers in PSRAM
    // and register the LVGL display driver.  After lvgl_start() the
    // display subsystem is ready to draw widgets.
    display_init_panel();
    lvgl_start();

    // Initialize the touch controller and register the LVGL input
    // device.  This also starts the I2C driver and probes the GT911
    // for its runtime I2C address.
    touch_init();

    // Create all UI objects (screens, labels, buttons) and register
    // their event callbacks.  This call must occur after LVGL is
    // initialized.
    create_ui();

    // Initialize the game logic.  This will set up the default
    // ReptileState, create the game event queue and attempt to load
    // any previously saved state from persistent storage.
    game_init();

    // Create the FreeRTOS tasks.  We distribute the heavier tasks
    // across the two cores of the ESP32‑P4.  The display task and
    // sensor task run on core 0, while the game logic and animation
    // tasks run on core 1.  Adjust stack sizes and priorities as
    // needed for your specific application.
    xTaskCreatePinnedToCore(display_task, "Display", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(sensor_task, "Input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(game_task,   "Game", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(anim_task,   "Anim", 3072, NULL, 2, NULL, 1);

    // At this point the tasks are running.  app_main() can return
    // safely because FreeRTOS will continue to schedule the tasks in
    // the background.
}