#include "ota_keys.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

static const char *TAG = "ota_keys";

esp_err_t ota_keys_get_pubkey(uint8_t out_pubkey[32])
{
    if (!out_pubkey) {
        return ESP_ERR_INVALID_ARG;
    }
#ifndef CONFIG_TRIBO_OTA_PUBKEY_BASE64
    ESP_LOGE(TAG, "CONFIG_TRIBO_OTA_PUBKEY_BASE64 not defined");
    return ESP_ERR_NOT_SUPPORTED;
#else
    static uint8_t cached[32];
    static bool loaded = false;
    if (!loaded) {
        const char *b64 = CONFIG_TRIBO_OTA_PUBKEY_BASE64;
        size_t len = strlen(b64);
        size_t out_len = 0;
        int rc = mbedtls_base64_decode(cached, sizeof(cached), &out_len,
                                       (const unsigned char *)b64, len);
        if (rc != 0 || out_len != sizeof(cached)) {
            ESP_LOGE(TAG, "Invalid OTA public key base64 (rc=%d, len=%zu)", rc, out_len);
            return ESP_ERR_INVALID_STATE;
        }
        loaded = true;
    }
    memcpy(out_pubkey, cached, sizeof(cached));
    return ESP_OK;
#endif
}

