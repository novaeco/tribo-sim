// Storage subsystem declarations

#pragma once

#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise persistent storage
void storage_init(void);

// Save the given reptile state to a file
bool storage_save_state(const ReptileState *state);

// Load a previously saved state
bool storage_load_state(ReptileState *state);

#ifdef __cplusplus
}
#endif
