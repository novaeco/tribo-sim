// Common type definitions for Tribo-Sim
// Separated to avoid circular dependencies between components

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Enumeration of reptile moods
typedef enum {
    MOOD_HAPPY = 0,
    MOOD_NEUTRAL,
    MOOD_SAD,
    MOOD_HUNGRY,
    MOOD_SLEEPY,
    MOOD_SICK,
    MOOD_PLAYFUL
} ReptileMood;

// Structure representing the state of the reptile
typedef struct {
    int health;          // Health (0-100)
    int hunger;          // Hunger (0-100, 0 = fed)
    int growth;          // Growth (0-100)
    float temperature;   // Virtual ambient temperature (C)
    bool heater_on;      // Heater state
    int cleanliness;     // Cleanliness (0-100, 100 = clean)
    int happiness;       // Happiness (0-100)
    ReptileMood mood;    // Current mood
    uint32_t age_ticks;  // Age in game ticks
    bool is_sleeping;    // Sleep state
} ReptileState;

// Enumeration of game events posted from UI callbacks
typedef enum {
    GAME_EVENT_FEED = 0,
    GAME_EVENT_HEAT_ON,
    GAME_EVENT_HEAT_OFF,
    GAME_EVENT_PLAY,
    GAME_EVENT_CLEAN,
    GAME_EVENT_SLEEP,
    GAME_EVENT_WAKE,
    GAME_EVENT_PAUSE,
    GAME_EVENT_RESUME
} GameEvent;
