#pragma once

#include "esp_err.h"

/**
 * @brief Initialise encrypted NVS storage (or plain NVS if encryption is disabled).
 *
 * This helper makes sure that the NVS key partition is generated on the first
 * boot and that the main NVS partition is formatted when required.
 */
esp_err_t storage_secure_init(void);

/**
 * @brief De-initialise NVS storage.
 */
esp_err_t storage_secure_deinit(void);

/**
 * @brief Erase the NVS user partition.
 */
esp_err_t storage_secure_erase(void);
