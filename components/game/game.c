// Implementation of the game logic for the reptile simulation
//
// Manages the evolution of the reptile’s state (health, hunger,
// growth and temperature) and responds to user interactions.  The
// game runs in its own FreeRTOS task which periodically updates
// these values and posts UI updates.  A FreeRTOS queue is used to
// handle events posted from the LVGL callbacks in display.c.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "game.h"
#include "sim_display.h"
#include "storage.h"

static const char *TAG = "GAME";

// Current reptile state.  This is updated in game_task() and can
// be read by other components if necessary.
ReptileState g_state;

// Indicates whether the game logic is currently paused
bool game_paused = false;

// FreeRTOS queue to hold incoming events from the UI
static QueueHandle_t s_event_queue;

// Forward declaration of a helper to clamp integer values between 0 and 100
static int clamp_int(int value, int min, int max);

void game_init(void)
{
    // Create the event queue (capacity 10 events)
    s_event_queue = xQueueCreate(10, sizeof(GameEvent));
    assert(s_event_queue != NULL);

    // Initialise default state
    g_state.health = 100;
    g_state.hunger = 0;
    g_state.growth = 0;
    g_state.temperature = 25.0f;
    g_state.heater_on = false;

    // Attempt to load previously saved state
    if (!storage_load_state(&g_state)) {
        ESP_LOGI(TAG, "Aucune sauvegarde trouvée, utilisation des valeurs par défaut");
    }
}

void game_post_event(GameEvent ev)
{
    if (!s_event_queue) {
        return;
    }
    xQueueSend(s_event_queue, &ev, 0);
}

// Main simulation loop for game logic
void game_task(void *arg)
{
    (void)arg;
    // Retrieve tick period from Kconfig (milliseconds)
    const uint32_t tick_ms = CONFIG_GAME_TICK_MS;
    const int hunger_inc = 5;     // increment hunger per tick
    const int health_dec_hunger = 2;
    const int health_dec_temp   = 2;
    const float temp_ideal_min = 26.0f;
    const float temp_ideal_max = 32.0f;
    const float temp_cooldown  = 0.1f;
    const float temp_heating   = 0.5f;
    int save_timer = 0;

    // Wait until the UI signals that the game has started
    while (!game_started) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    while (1) {
        // Process all queued events
        GameEvent ev;
        while (xQueueReceive(s_event_queue, &ev, 0) == pdTRUE) {
            switch (ev) {
            case GAME_EVENT_FEED:
                g_state.hunger = clamp_int(g_state.hunger - 20, 0, 100);
                if (g_state.health < 100) {
                    g_state.health = clamp_int(g_state.health + 5, 0, 100);
                }
                storage_save_state(&g_state);
                break;
            case GAME_EVENT_HEAT_ON:
                g_state.heater_on = true;
                break;
            case GAME_EVENT_HEAT_OFF:
                g_state.heater_on = false;
                break;
            case GAME_EVENT_PAUSE:
                game_paused = true;
                break;
            case GAME_EVENT_RESUME:
                game_paused = false;
                break;
            default:
                break;
            }
        }

        if (!game_paused) {
            // Update temperature
            if (g_state.heater_on) {
                g_state.temperature += temp_heating;
                if (g_state.temperature > 40.0f) {
                    g_state.temperature = 40.0f;
                }
            } else {
                g_state.temperature -= temp_cooldown;
                if (g_state.temperature < 15.0f) {
                    g_state.temperature = 15.0f;
                }
            }
            // Update hunger
            g_state.hunger = clamp_int(g_state.hunger + hunger_inc, 0, 100);
            // Decrease health if hunger high or temperature out of range
            if (g_state.hunger >= 80) {
                g_state.health = clamp_int(g_state.health - health_dec_hunger, 0, 100);
            }
            if (g_state.temperature < temp_ideal_min || g_state.temperature > temp_ideal_max) {
                g_state.health = clamp_int(g_state.health - health_dec_temp, 0, 100);
            }
            // Increase growth slowly when conditions are good
            if (g_state.health > 80 && g_state.hunger < 30) {
                g_state.growth = clamp_int(g_state.growth + 1, 0, 100);
            }
            // If health drops to zero, reset the game
            if (g_state.health == 0) {
                ESP_LOGW(TAG, "L'animal est mort - réinitialisation du jeu");
                g_state.health = 100;
                g_state.hunger = 0;
                g_state.growth = 0;
                g_state.temperature = 25.0f;
                g_state.heater_on = false;
            }
        }

        // Compose status string and update UI
        char status_text[64];
        snprintf(status_text, sizeof(status_text),
                 "Santé: %d\nFaim: %d\nTemp: %.1f°C",
                 g_state.health, g_state.hunger, g_state.temperature);
        display_update_status_async(status_text);

        // Periodically save state every minute (if not paused)
        if (!game_paused) {
            save_timer += tick_ms;
            if (save_timer >= 60000) {
                storage_save_state(&g_state);
                save_timer = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }
}

static int clamp_int(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}