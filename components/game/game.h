// Game logic declarations for the reptile simulation

#pragma once

#include <stdbool.h>

// Structure representing the state of the reptile.  Values are
// clamped between sensible ranges (0‑100 for integral fields).
typedef struct {
    int health;       // Santé (0–100)
    int hunger;       // Faim (0–100, 0 = repu)
    int growth;       // Croissance (0–100)
    float temperature;  // Température ambiante virtuelle (°C)
    bool heater_on;     // État du chauffage
} ReptileState;

// Enumeration of game events posted from UI callbacks
typedef enum {
    GAME_EVENT_FEED = 0,
    GAME_EVENT_HEAT_ON,
    GAME_EVENT_HEAT_OFF,
    GAME_EVENT_PAUSE,
    GAME_EVENT_RESUME
} GameEvent;

// Externally visible state object representing the current reptile state
extern ReptileState g_state;

// Flag indicating whether the game is paused.  If paused, the game
// task will skip state updates until resumed.
extern bool game_paused;

// Initialise the game logic subsystem.  Creates the event queue,
// sets up the default reptile state and loads any saved state from
// persistent storage.
void game_init(void);

// FreeRTOS task implementing the simulation loop.  It processes
// queued events, updates the reptile state, saves the state
// periodically and notifies the UI via display_update_status_async().
void game_task(void *arg);

// Post an event to the game logic.  This can be called from any
// task or LVGL callback.  Events are queued to be processed by
// game_task().
void game_post_event(GameEvent ev);
