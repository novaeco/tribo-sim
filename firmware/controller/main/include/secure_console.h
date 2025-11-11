#pragma once

#include "esp_err.h"

/**
 * @brief Initialise and start the secure serial console REPL.
 *
 * The console exposes high-privilege commands (e.g. bootstrap token
 * retrieval) and therefore is only meant to be reachable via the
 * physically secured maintenance port.
 */
esp_err_t secure_console_start(void);
