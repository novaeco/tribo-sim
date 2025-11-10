#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/**
 * @brief Load TLS credentials and API secrets from secure storage.
 *
 * This function is idempotent. On the first boot it provisions a new
 * self-signed certificate/key pair as well as a bearer token secret.
 */
esp_err_t credentials_init(void);

/**
 * @brief Retrieve the DER/PEM encoded server certificate buffer.
 */
const uint8_t *credentials_server_cert(size_t *len);

/**
 * @brief Retrieve the PEM encoded private key buffer.
 */
const uint8_t *credentials_server_key(size_t *len);

/**
 * @brief Parse an Authorization header and validate the bearer token.
 */
bool credentials_authorize_bearer(const char *authorization_header);

/**
 * @brief Return the bootstrap bearer token (only available on the boot when generated).
 */
const char *credentials_bootstrap_token(void);

/**
 * @brief Force regeneration of TLS and/or API secrets.
 */
esp_err_t credentials_rotate(bool rotate_cert, bool rotate_token);
