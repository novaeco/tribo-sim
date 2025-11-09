#include "ota_stream.h"

#include <stddef.h>

esp_err_t ota_stream_chunks(const uint8_t *data, size_t len, size_t chunk_size, ota_chunk_cb_t cb, void *ctx)
{
    if ((!data && len > 0) || !cb || chunk_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    size_t offset = 0;
    while (offset < len) {
        size_t remain = len - offset;
        size_t chunk = remain < chunk_size ? remain : chunk_size;
        esp_err_t err = cb(data + offset, chunk, ctx);
        if (err != ESP_OK) {
            return err;
        }
        offset += chunk;
    }
    return ESP_OK;
}

