#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*ota_chunk_cb_t)(const uint8_t *chunk, size_t len, void *ctx);

esp_err_t ota_stream_chunks(const uint8_t *data, size_t len, size_t chunk_size, ota_chunk_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

