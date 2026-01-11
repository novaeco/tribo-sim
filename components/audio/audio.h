// Audio subsystem for sound effects and feedback
// Supports buzzer (PWM) and DAC output

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sound effect types
typedef enum {
    SOUND_NONE = 0,
    SOUND_FEED,         // Happy eating sound
    SOUND_PLAY,         // Playful chirp
    SOUND_CLEAN,        // Sparkle/clean sound
    SOUND_HEAT_ON,      // Heat lamp click
    SOUND_HEAT_OFF,     // Heat lamp off
    SOUND_HAPPY,        // Happy melody
    SOUND_SAD,          // Sad tone
    SOUND_SICK,         // Warning beep
    SOUND_HUNGRY,       // Stomach growl tone
    SOUND_SLEEP,        // Lullaby note
    SOUND_WAKE,         // Wake up jingle
    SOUND_DEATH,        // Game over sound
    SOUND_BUTTON,       // UI button click
    SOUND_START,        // Game start fanfare
} SoundEffect;

// Initialize the audio subsystem
// Returns true on success
bool audio_init(void);

// Play a sound effect (non-blocking)
void audio_play(SoundEffect effect);

// Stop any currently playing sound
void audio_stop(void);

// Set master volume (0-100)
void audio_set_volume(uint8_t volume);

// Get current volume
uint8_t audio_get_volume(void);

// Enable/disable audio
void audio_set_enabled(bool enabled);

// Check if audio is enabled
bool audio_is_enabled(void);

// Audio task for background sound generation
void audio_task(void *arg);

#ifdef __cplusplus
}
#endif
