// Implementation of the animation subsystem
//
// This task periodically moves the pet sprite left and right.  The
// movement is performed asynchronously via lv_async_call() to avoid
// calling LVGL functions from a non‑LVGL thread directly.

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sim_display.h"

// game_started is defined in main.c; declare it here to silence
// compiler warnings.  It is set to true by the UI when the user
// presses the "Commencer" button.
extern bool game_started;

// Helper callback to move the pet object by a given delta on the X axis.
static void anim_move_async(void *param)
{
    int dx = *(int *)param;
    // Update the position of the pet object
    if (pet_obj) {
        lv_coord_t x = lv_obj_get_x(pet_obj);
        lv_coord_t y = lv_obj_get_y(pet_obj);
        lv_obj_set_pos(pet_obj, x + dx, y);
    }
    free(param);
}

void anim_task(void *arg)
{
    (void)arg;
    const int amplitude = 20;
    int direction = 1;
    // Wait until game has started
    while (!game_started) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    while (1) {
        // Skip animation if the game is paused
        extern bool game_paused;
        if (!game_paused) {
            // Allocate memory for dx and post asynchronous update
            int *dx = malloc(sizeof(int));
            if (dx) {
                *dx = amplitude * direction;
                lv_async_call(anim_move_async, dx);
            }
            direction = -direction;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
