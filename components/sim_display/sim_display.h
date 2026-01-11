// SPDX-License-Identifier: Apache-2.0
//
// Public header for the reptile simulation display subsystem.
//
// Provides declarations for the display initialisation, LVGL
// startup, UI creation, asynchronous status updates and the
// display task.  Also exports certain LVGL objects that are
// manipulated by other components (e.g. the game logic).

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Expose LVGL objects so other modules can update them.
extern lv_obj_t *pet_obj;
extern lv_obj_t *label_status;
extern lv_obj_t *label_perf;

// Starts up the JD9165 panel and configures the MIPI‑DSI bus.
void display_init_panel(void);

// Initializes LVGL and registers the display driver.
void lvgl_start(void);

// Creates the UI screens.  Must be called after lvgl_start().
void create_ui(void);

// Schedules an asynchronous update of the status label.  The string
// will be copied internally and freed after the update.
void display_update_status_async(const char *status);

// Main display task.  Call this in a FreeRTOS task.
void display_task(void *arg);

#ifdef __cplusplus
}
#endif