#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t controller_cert_store_init(void);
bool      controller_cert_store_is_ready(void);
bool      controller_cert_store_has_custom(void);
const char *controller_cert_store_get(void);
size_t    controller_cert_store_length(void);
esp_err_t controller_cert_store_import(const uint8_t *data, size_t len);
esp_err_t controller_cert_store_import_from_file(const char *path);
esp_err_t controller_cert_store_clear(void);

#ifdef __cplusplus
}
#endif

