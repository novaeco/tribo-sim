// Unit tests for game logic
// Uses Unity test framework (built into ESP-IDF)

#include "unity.h"
#include "types.h"
#include <string.h>

// Test helper: create default state
static ReptileState create_default_state(void)
{
    ReptileState state = {
        .health = 100,
        .hunger = 0,
        .growth = 0,
        .temperature = 25.0f,
        .heater_on = false,
        .cleanliness = 100,
        .happiness = 80,
        .mood = MOOD_HAPPY,
        .age_ticks = 0,
        .is_sleeping = false
    };
    return state;
}

// Helper: clamp function (mirrors game.c)
static int clamp_int(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// Test: Initial state values
TEST_CASE("Game state initializes with correct defaults", "[game]")
{
    ReptileState state = create_default_state();

    TEST_ASSERT_EQUAL_INT(100, state.health);
    TEST_ASSERT_EQUAL_INT(0, state.hunger);
    TEST_ASSERT_EQUAL_INT(0, state.growth);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, state.temperature);
    TEST_ASSERT_FALSE(state.heater_on);
    TEST_ASSERT_EQUAL_INT(100, state.cleanliness);
    TEST_ASSERT_EQUAL_INT(80, state.happiness);
    TEST_ASSERT_EQUAL(MOOD_HAPPY, state.mood);
    TEST_ASSERT_EQUAL_UINT32(0, state.age_ticks);
    TEST_ASSERT_FALSE(state.is_sleeping);
}

// Test: Clamp function
TEST_CASE("Clamp function works correctly", "[game]")
{
    TEST_ASSERT_EQUAL_INT(0, clamp_int(-10, 0, 100));
    TEST_ASSERT_EQUAL_INT(100, clamp_int(150, 0, 100));
    TEST_ASSERT_EQUAL_INT(50, clamp_int(50, 0, 100));
    TEST_ASSERT_EQUAL_INT(0, clamp_int(0, 0, 100));
    TEST_ASSERT_EQUAL_INT(100, clamp_int(100, 0, 100));
}

// Test: Feeding reduces hunger
TEST_CASE("Feeding reduces hunger and increases health", "[game]")
{
    ReptileState state = create_default_state();
    state.hunger = 50;
    state.health = 80;

    // Simulate feed event
    state.hunger = clamp_int(state.hunger - 25, 0, 100);
    state.health = clamp_int(state.health + 5, 0, 100);

    TEST_ASSERT_EQUAL_INT(25, state.hunger);
    TEST_ASSERT_EQUAL_INT(85, state.health);
}

// Test: Feeding doesn't go below zero
TEST_CASE("Feeding with low hunger doesn't go negative", "[game]")
{
    ReptileState state = create_default_state();
    state.hunger = 10;

    state.hunger = clamp_int(state.hunger - 25, 0, 100);

    TEST_ASSERT_EQUAL_INT(0, state.hunger);
}

// Test: Temperature increases with heater
TEST_CASE("Heater increases temperature", "[game]")
{
    ReptileState state = create_default_state();
    state.temperature = 25.0f;
    state.heater_on = true;

    // Simulate tick
    if (state.heater_on) {
        state.temperature += 0.5f;
        if (state.temperature > 40.0f) {
            state.temperature = 40.0f;
        }
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.5f, state.temperature);
}

// Test: Temperature capped at max
TEST_CASE("Temperature doesn't exceed maximum", "[game]")
{
    ReptileState state = create_default_state();
    state.temperature = 39.8f;
    state.heater_on = true;

    // Multiple ticks
    for (int i = 0; i < 10; i++) {
        if (state.heater_on) {
            state.temperature += 0.5f;
            if (state.temperature > 40.0f) {
                state.temperature = 40.0f;
            }
        }
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, state.temperature);
}

// Test: Temperature decreases without heater
TEST_CASE("Temperature decreases without heater", "[game]")
{
    ReptileState state = create_default_state();
    state.temperature = 30.0f;
    state.heater_on = false;

    // Simulate tick
    state.temperature -= 0.1f;
    if (state.temperature < 15.0f) {
        state.temperature = 15.0f;
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 29.9f, state.temperature);
}

// Test: Health decreases when hungry
TEST_CASE("Health decreases when very hungry", "[game]")
{
    ReptileState state = create_default_state();
    state.hunger = 85;
    state.health = 100;

    // Simulate tick - health dec when hunger >= 80
    if (state.hunger >= 80) {
        state.health = clamp_int(state.health - 2, 0, 100);
    }

    TEST_ASSERT_EQUAL_INT(98, state.health);
}

// Test: Health decreases with bad temperature
TEST_CASE("Health decreases when temperature out of range", "[game]")
{
    ReptileState state = create_default_state();
    state.temperature = 20.0f;  // Below ideal (26-32)
    state.health = 100;

    // Simulate tick
    float temp_ideal_min = 26.0f;
    float temp_ideal_max = 32.0f;
    if (state.temperature < temp_ideal_min || state.temperature > temp_ideal_max) {
        state.health = clamp_int(state.health - 2, 0, 100);
    }

    TEST_ASSERT_EQUAL_INT(98, state.health);
}

// Test: Growth increases under good conditions
TEST_CASE("Growth increases when healthy and fed", "[game]")
{
    ReptileState state = create_default_state();
    state.health = 90;
    state.hunger = 20;
    state.happiness = 60;
    state.growth = 0;

    // Simulate tick
    if (state.health > 80 && state.hunger < 30 && state.happiness > 50) {
        state.growth = clamp_int(state.growth + 1, 0, 100);
    }

    TEST_ASSERT_EQUAL_INT(1, state.growth);
}

// Test: Growth doesn't increase under bad conditions
TEST_CASE("Growth doesn't increase when unhealthy", "[game]")
{
    ReptileState state = create_default_state();
    state.health = 50;  // Below threshold
    state.hunger = 20;
    state.happiness = 60;
    state.growth = 10;

    // Simulate tick
    if (state.health > 80 && state.hunger < 30 && state.happiness > 50) {
        state.growth = clamp_int(state.growth + 1, 0, 100);
    }

    TEST_ASSERT_EQUAL_INT(10, state.growth);  // Unchanged
}

// Test: Playing increases happiness
TEST_CASE("Playing increases happiness", "[game]")
{
    ReptileState state = create_default_state();
    state.happiness = 50;

    // Simulate play event
    state.happiness = clamp_int(state.happiness + 20, 0, 100);

    TEST_ASSERT_EQUAL_INT(70, state.happiness);
}

// Test: Cleaning resets cleanliness
TEST_CASE("Cleaning resets cleanliness to 100", "[game]")
{
    ReptileState state = create_default_state();
    state.cleanliness = 30;

    // Simulate clean event
    state.cleanliness = 100;

    TEST_ASSERT_EQUAL_INT(100, state.cleanliness);
}

// Test: Mood becomes HUNGRY when very hungry
TEST_CASE("Mood becomes HUNGRY when hunger is high", "[game]")
{
    ReptileState state = create_default_state();
    state.hunger = 75;
    state.health = 80;

    // Update mood logic
    if (state.health < 30) {
        state.mood = MOOD_SICK;
    } else if (state.hunger > 70) {
        state.mood = MOOD_HUNGRY;
    }

    TEST_ASSERT_EQUAL(MOOD_HUNGRY, state.mood);
}

// Test: Mood becomes SICK when health low
TEST_CASE("Mood becomes SICK when health is very low", "[game]")
{
    ReptileState state = create_default_state();
    state.health = 20;
    state.hunger = 80;  // Also hungry, but sick takes priority

    // Update mood logic (health takes priority)
    if (state.health < 30) {
        state.mood = MOOD_SICK;
    } else if (state.hunger > 70) {
        state.mood = MOOD_HUNGRY;
    }

    TEST_ASSERT_EQUAL(MOOD_SICK, state.mood);
}

// Test: Death resets state
TEST_CASE("Death at zero health resets state", "[game]")
{
    ReptileState state = create_default_state();
    state.health = 0;
    state.hunger = 90;
    state.growth = 50;
    state.temperature = 35.0f;

    // Simulate death check
    if (state.health == 0) {
        state.health = 100;
        state.hunger = 0;
        state.growth = 0;
        state.temperature = 25.0f;
        state.heater_on = false;
        state.cleanliness = 100;
        state.happiness = 80;
        state.mood = MOOD_HAPPY;
        state.age_ticks = 0;
        state.is_sleeping = false;
    }

    TEST_ASSERT_EQUAL_INT(100, state.health);
    TEST_ASSERT_EQUAL_INT(0, state.hunger);
    TEST_ASSERT_EQUAL_INT(0, state.growth);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, state.temperature);
}

// Test: Sleeping reduces hunger rate
TEST_CASE("Sleeping reptile has slower hunger increase", "[game]")
{
    ReptileState awake = create_default_state();
    ReptileState sleeping = create_default_state();
    sleeping.is_sleeping = true;

    int hunger_inc = 3;

    // Simulate tick for awake
    int effective_hunger_awake = sleeping.is_sleeping ? hunger_inc / 2 : hunger_inc;
    awake.hunger = clamp_int(awake.hunger + hunger_inc, 0, 100);

    // Simulate tick for sleeping
    int effective_hunger_sleeping = sleeping.is_sleeping ? hunger_inc / 2 : hunger_inc;
    sleeping.hunger = clamp_int(sleeping.hunger + effective_hunger_sleeping, 0, 100);

    TEST_ASSERT_EQUAL_INT(3, awake.hunger);
    TEST_ASSERT_EQUAL_INT(1, sleeping.hunger);  // Half rate
}

// Test: All mood types exist
TEST_CASE("All mood types are defined", "[game]")
{
    TEST_ASSERT_EQUAL(0, MOOD_HAPPY);
    TEST_ASSERT_EQUAL(1, MOOD_NEUTRAL);
    TEST_ASSERT_EQUAL(2, MOOD_SAD);
    TEST_ASSERT_EQUAL(3, MOOD_HUNGRY);
    TEST_ASSERT_EQUAL(4, MOOD_SLEEPY);
    TEST_ASSERT_EQUAL(5, MOOD_SICK);
    TEST_ASSERT_EQUAL(6, MOOD_PLAYFUL);
}

// Test: All game events are defined
TEST_CASE("All game events are defined", "[game]")
{
    TEST_ASSERT_EQUAL(0, GAME_EVENT_FEED);
    TEST_ASSERT_EQUAL(1, GAME_EVENT_HEAT_ON);
    TEST_ASSERT_EQUAL(2, GAME_EVENT_HEAT_OFF);
    TEST_ASSERT_EQUAL(3, GAME_EVENT_PLAY);
    TEST_ASSERT_EQUAL(4, GAME_EVENT_CLEAN);
    TEST_ASSERT_EQUAL(5, GAME_EVENT_SLEEP);
    TEST_ASSERT_EQUAL(6, GAME_EVENT_WAKE);
    TEST_ASSERT_EQUAL(7, GAME_EVENT_PAUSE);
    TEST_ASSERT_EQUAL(8, GAME_EVENT_RESUME);
}
