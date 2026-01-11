// Implementation of the game logic for the reptile simulation
//
// Enhanced with new gameplay mechanics: play, clean, mood system,
// cleanliness tracking, happiness, and day/night cycle awareness.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "game.h"
#include "sim_display.h"
#include "storage.h"

static const char *TAG = "GAME";

// Current reptile state
ReptileState g_state;

// Indicates whether the game logic is currently paused
bool game_paused = false;

// FreeRTOS queue to hold incoming events from the UI
static QueueHandle_t s_event_queue;

// Forward declarations
static int clamp_int(int value, int min, int max);
static void update_mood(void);
static bool is_night_time(void);

// Mood strings for display
const char *game_get_mood_string(ReptileMood mood)
{
    switch (mood) {
        case MOOD_HAPPY:   return "Heureux";
        case MOOD_NEUTRAL: return "Neutre";
        case MOOD_SAD:     return "Triste";
        case MOOD_HUNGRY:  return "Affame";
        case MOOD_SLEEPY:  return "Fatigue";
        case MOOD_SICK:    return "Malade";
        case MOOD_PLAYFUL: return "Joueur";
        default:           return "???";
    }
}

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
    g_state.cleanliness = 100;
    g_state.happiness = 80;
    g_state.mood = MOOD_HAPPY;
    g_state.age_ticks = 0;
    g_state.is_sleeping = false;

    // Attempt to load previously saved state
    if (!storage_load_state(&g_state)) {
        ESP_LOGI(TAG, "No save found, using default values");
    }

    // Update mood based on initial state
    update_mood();
}

void game_post_event(GameEvent ev)
{
    if (!s_event_queue) {
        return;
    }
    xQueueSend(s_event_queue, &ev, 0);
}

// Check if it's night time (20:00 - 07:00)
static bool is_night_time(void)
{
    time_t now;
    time(&now);
    struct tm *local = localtime(&now);
    if (local) {
        return (local->tm_hour >= 20 || local->tm_hour < 7);
    }
    return false;
}

// Update the mood based on current stats
static void update_mood(void)
{
    if (g_state.health < 30) {
        g_state.mood = MOOD_SICK;
    } else if (g_state.hunger > 70) {
        g_state.mood = MOOD_HUNGRY;
    } else if (g_state.is_sleeping || (is_night_time() && g_state.happiness < 50)) {
        g_state.mood = MOOD_SLEEPY;
    } else if (g_state.happiness < 30) {
        g_state.mood = MOOD_SAD;
    } else if (g_state.happiness > 80 && g_state.health > 70) {
        g_state.mood = MOOD_HAPPY;
    } else if (g_state.happiness > 60) {
        g_state.mood = MOOD_PLAYFUL;
    } else {
        g_state.mood = MOOD_NEUTRAL;
    }
}

// Main simulation loop for game logic
void game_task(void *arg)
{
    (void)arg;
    // Retrieve tick period from Kconfig (milliseconds)
    const uint32_t tick_ms = CONFIG_GAME_TICK_MS;

    // Game constants
    const int hunger_inc = 3;           // Hunger increase per tick
    const int health_dec_hunger = 2;    // Health decrease when hungry
    const int health_dec_temp = 2;      // Health decrease when temp out of range
    const int health_dec_dirty = 1;     // Health decrease when dirty
    const int cleanliness_dec = 2;      // Cleanliness decrease per tick
    const int happiness_dec = 1;        // Happiness decrease per tick
    const float temp_ideal_min = 26.0f;
    const float temp_ideal_max = 32.0f;
    const float temp_cooldown = 0.1f;
    const float temp_heating = 0.5f;

    int save_timer = 0;

    // Wait until the UI signals that the game has started
    while (!game_started) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGI(TAG, "Game started!");

    while (1) {
        // Process all queued events
        GameEvent ev;
        while (xQueueReceive(s_event_queue, &ev, 0) == pdTRUE) {
            switch (ev) {
            case GAME_EVENT_FEED:
                if (!g_state.is_sleeping) {
                    g_state.hunger = clamp_int(g_state.hunger - 25, 0, 100);
                    g_state.health = clamp_int(g_state.health + 5, 0, 100);
                    g_state.happiness = clamp_int(g_state.happiness + 10, 0, 100);
                    ESP_LOGI(TAG, "Fed the reptile! Hunger: %d", g_state.hunger);
                    storage_save_state(&g_state);
                }
                break;

            case GAME_EVENT_HEAT_ON:
                g_state.heater_on = true;
                ESP_LOGI(TAG, "Heater turned ON");
                break;

            case GAME_EVENT_HEAT_OFF:
                g_state.heater_on = false;
                ESP_LOGI(TAG, "Heater turned OFF");
                break;

            case GAME_EVENT_PLAY:
                if (!g_state.is_sleeping && g_state.health > 20) {
                    g_state.happiness = clamp_int(g_state.happiness + 20, 0, 100);
                    g_state.hunger = clamp_int(g_state.hunger + 5, 0, 100);  // Playing makes hungry
                    ESP_LOGI(TAG, "Played with reptile! Happiness: %d", g_state.happiness);
                }
                break;

            case GAME_EVENT_CLEAN:
                g_state.cleanliness = 100;
                g_state.happiness = clamp_int(g_state.happiness + 10, 0, 100);
                ESP_LOGI(TAG, "Cleaned the terrarium!");
                break;

            case GAME_EVENT_SLEEP:
                g_state.is_sleeping = true;
                ESP_LOGI(TAG, "Reptile is now sleeping");
                break;

            case GAME_EVENT_WAKE:
                g_state.is_sleeping = false;
                g_state.health = clamp_int(g_state.health + 10, 0, 100);  // Rest heals
                ESP_LOGI(TAG, "Reptile woke up!");
                break;

            case GAME_EVENT_PAUSE:
                game_paused = true;
                ESP_LOGI(TAG, "Game paused");
                break;

            case GAME_EVENT_RESUME:
                game_paused = false;
                ESP_LOGI(TAG, "Game resumed");
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

            // Sleeping reptile has slower metabolism
            int effective_hunger_inc = g_state.is_sleeping ? hunger_inc / 2 : hunger_inc;

            // Update hunger
            g_state.hunger = clamp_int(g_state.hunger + effective_hunger_inc, 0, 100);

            // Update cleanliness (decreases over time)
            g_state.cleanliness = clamp_int(g_state.cleanliness - cleanliness_dec, 0, 100);

            // Update happiness (decreases slowly over time)
            if (!g_state.is_sleeping) {
                g_state.happiness = clamp_int(g_state.happiness - happiness_dec, 0, 100);
            }

            // Health effects
            if (g_state.hunger >= 80) {
                g_state.health = clamp_int(g_state.health - health_dec_hunger, 0, 100);
            }
            if (g_state.temperature < temp_ideal_min || g_state.temperature > temp_ideal_max) {
                g_state.health = clamp_int(g_state.health - health_dec_temp, 0, 100);
            }
            if (g_state.cleanliness < 30) {
                g_state.health = clamp_int(g_state.health - health_dec_dirty, 0, 100);
            }

            // Growth when conditions are good
            if (g_state.health > 80 && g_state.hunger < 30 && g_state.happiness > 50) {
                g_state.growth = clamp_int(g_state.growth + 1, 0, 100);
            }

            // Age the reptile
            g_state.age_ticks++;

            // Update mood based on current state
            update_mood();

            // Check for death
            if (g_state.health == 0) {
                ESP_LOGW(TAG, "The reptile has died - resetting game");
                g_state.health = 100;
                g_state.hunger = 0;
                g_state.growth = 0;
                g_state.temperature = 25.0f;
                g_state.heater_on = false;
                g_state.cleanliness = 100;
                g_state.happiness = 80;
                g_state.mood = MOOD_HAPPY;
                g_state.age_ticks = 0;
                g_state.is_sleeping = false;
            }

            // Update the display with current state
            display_update_game_state(&g_state);
        }

        // Compose status string for legacy compatibility
        char status_text[96];
        snprintf(status_text, sizeof(status_text),
                 "Sante: %d\nFaim: %d\nTemp: %.1f°C\nHumeur: %s",
                 g_state.health, g_state.hunger, g_state.temperature,
                 game_get_mood_string(g_state.mood));
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
