#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define OTA_MANIFEST_MAX_VERSION_LEN 32
#define OTA_MANIFEST_MAX_SIGNED_AT_LEN 32

typedef enum {
    OTA_TARGET_CONTROLLER = 0,
    OTA_TARGET_DOME = 1,
} ota_target_t;

typedef struct {
    ota_target_t target;
    char version[OTA_MANIFEST_MAX_VERSION_LEN];
    char signed_at[OTA_MANIFEST_MAX_SIGNED_AT_LEN];
    bool has_signed_at;
    uint32_t image_size;
    uint8_t image_sha256[32];
    uint8_t signature[64];
} ota_manifest_t;

/**
 * @brief Parse a JSON manifest payload into a ota_manifest_t structure.
 *
 * The manifest must follow the schema:
 * {
 *   "format": "tribo-ota-manifest",
 *   "format_version": 1,
 *   "target": "controller" | "dome",
 *   "fw_version": "X.Y.Z",
 *   "image_size": <uint32>,
 *   "image_sha256": "<hex-64>",
 *   "signature": "<base64-encoded Ed25519 signature>",
 *   "signed_at": "ISO-8601" (optional)
 * }
 */
esp_err_t ota_manifest_parse(const char *json, size_t len, ota_manifest_t *out);

/**
 * @brief Verify the Ed25519 signature on a manifest against the configured public key.
 */
esp_err_t ota_manifest_verify(const ota_manifest_t *manifest);

/**
 * @brief Return true if the manifest describes the requested target component.
 */
bool ota_manifest_is_target(const ota_manifest_t *manifest, ota_target_t target);

/**
 * @brief Return a human readable string for a target enum.
 */
const char *ota_manifest_target_name(ota_target_t target);

/**
 * @brief Convert binary SHA-256 digest to lowercase hex representation.
 */
void ota_manifest_sha256_to_hex(const uint8_t digest[32], char out_hex[65]);

/**
 * @brief Compare semantic version strings. Returns >0 if candidate is newer.
 */
int ota_manifest_compare_versions(const char *current, const char *candidate);

