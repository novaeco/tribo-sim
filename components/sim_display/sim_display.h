// SPDX-License-Identifier: Apache-2.0
//
// Public header for the reptile simulation display subsystem.

#pragma once

#include "lvgl.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Expose LVGL objects so other modules can update them.
extern lv_obj_t *pet_obj;
extern lv_obj_t *label_status;
extern lv_obj_t *label_perf;

// Starts up the JD9165 panel and configures the MIPI-DSI bus.
void display_init_panel(void);

// Initializes LVGL and registers the display driver.
void lvgl_start(void);

// Creates the UI screens.  Must be called after lvgl_start().
void create_ui(void);

// Schedules an asynchronous update of the status label.
void display_update_status_async(const char *status);

// Update UI elements based on game state (bars, arc, pet color)
void display_update_game_state(const ReptileState *state);

// Main display task.  Call this in a FreeRTOS task.
void display_task(void *arg);

#ifdef __cplusplus
}
#endif
