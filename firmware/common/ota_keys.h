#pragma once

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Retrieve the Ed25519 OTA signing public key from configuration.
 */
esp_err_t ota_keys_get_pubkey(uint8_t out_pubkey[32]);

