// Game logic declarations for the reptile simulation

#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Externally visible state object representing the current reptile state
extern ReptileState g_state;

// Flag indicating whether the game is paused
extern bool game_paused;

// Flag indicating whether the game has started
extern bool game_started;

// Initialise the game logic subsystem
void game_init(void);

// FreeRTOS task implementing the simulation loop
void game_task(void *arg);

// Post an event to the game logic
void game_post_event(GameEvent ev);

// Get the current mood as a string (for display)
const char *game_get_mood_string(ReptileMood mood);

#ifdef __cplusplus
}
#endif
