// Storage subsystem declarations

#pragma once

#include <stdbool.h>
#include "game.h"

// Initialise persistent storage.  Attempts to mount an SD card if
// USE_SD_CARD is enabled, otherwise mounts SPIFFS.  If mounting the
// SD card fails, falls back to SPIFFS.  Safe to call multiple
// times.
void storage_init(void);

// Save the given reptile state to a file on the mounted storage
// device.  Returns true on success, false on error.
bool storage_save_state(const ReptileState *state);

// Load a previously saved state into the provided structure.  If
// successful returns true, otherwise returns false and leaves
// `state` unchanged.
bool storage_load_state(ReptileState *state);
