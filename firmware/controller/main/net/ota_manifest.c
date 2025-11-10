#include "ota_manifest.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"

#include "ota_keys.h"
#include "monocypher.h"

static const char *TAG = "ota_manifest";

static int hex_to_byte(char high, char low)
{
    if (!isxdigit((unsigned char)high) || !isxdigit((unsigned char)low)) {
        return -1;
    }
    int hi = isdigit((unsigned char)high) ? high - '0' : (tolower((unsigned char)high) - 'a' + 10);
    int lo = isdigit((unsigned char)low) ? low - '0' : (tolower((unsigned char)low) - 'a' + 10);
    return (hi << 4) | lo;
}

static bool parse_sha256_hex(const char *hex, uint8_t out[32])
{
    if (!hex) {
        return false;
    }
    size_t len = strlen(hex);
    if (len != 64) {
        return false;
    }
    for (size_t i = 0; i < 32; ++i) {
        int value = hex_to_byte(hex[2 * i], hex[2 * i + 1]);
        if (value < 0) {
            return false;
        }
        out[i] = (uint8_t)value;
    }
    return true;
}

static size_t build_signed_message(const ota_manifest_t *m, uint8_t *out, size_t out_len)
{
    char signed_at_buf[OTA_MANIFEST_MAX_SIGNED_AT_LEN];
    const char *signed_at = "";
    if (m->has_signed_at) {
        strncpy(signed_at_buf, m->signed_at, sizeof(signed_at_buf));
        signed_at_buf[sizeof(signed_at_buf) - 1] = '\0';
        signed_at = signed_at_buf;
    }

    char sha_hex[65];
    ota_manifest_sha256_to_hex(m->image_sha256, sha_hex);

    int needed = snprintf(NULL, 0,
                          "format:tribo-ota-manifest\n"
                          "format_version:1\n"
                          "target:%s\n"
                          "fw_version:%s\n"
                          "image_size:%" PRIu32 "\n"
                          "image_sha256:%s\n"
                          "signed_at:%s\n",
                          ota_manifest_target_name(m->target),
                          m->version,
                          m->image_size,
                          sha_hex,
                          signed_at);
    if (needed <= 0) {
        return 0;
    }
    if (!out) {
        return (size_t)needed;
    }
    if ((size_t)needed > out_len) {
        return 0;
    }
    snprintf((char *)out, out_len, "format:tribo-ota-manifest\n"
                                  "format_version:1\n"
                                  "target:%s\n"
                                  "fw_version:%s\n"
                                  "image_size:%" PRIu32 "\n"
                                  "image_sha256:%s\n"
                                  "signed_at:%s\n",
             ota_manifest_target_name(m->target),
             m->version,
             m->image_size,
             sha_hex,
             signed_at);
    return (size_t)needed;
}

static ota_target_t parse_target(const char *value)
{
    if (!value) {
        return OTA_TARGET_CONTROLLER; // default, will be rejected later if mismatch
    }
    if (strcasecmp(value, "controller") == 0) {
        return OTA_TARGET_CONTROLLER;
    }
    if (strcasecmp(value, "dome") == 0) {
        return OTA_TARGET_DOME;
    }
    return OTA_TARGET_CONTROLLER;
}

esp_err_t ota_manifest_parse(const char *json, size_t len, ota_manifest_t *out)
{
    if (!json || len == 0 || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGE(TAG, "Manifest JSON parse error");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    cJSON *format = cJSON_GetObjectItem(root, "format");
    cJSON *format_version = cJSON_GetObjectItem(root, "format_version");
    cJSON *target = cJSON_GetObjectItem(root, "target");
    cJSON *fw_version = cJSON_GetObjectItem(root, "fw_version");
    cJSON *image_size = cJSON_GetObjectItem(root, "image_size");
    cJSON *image_sha256 = cJSON_GetObjectItem(root, "image_sha256");
    cJSON *signature = cJSON_GetObjectItem(root, "signature");
    cJSON *signed_at = cJSON_GetObjectItem(root, "signed_at");

    if (!cJSON_IsString(format) || strcmp(format->valuestring, "tribo-ota-manifest") != 0) {
        ESP_LOGE(TAG, "Unsupported manifest format");
        goto out;
    }
    if (!cJSON_IsNumber(format_version) || format_version->valuedouble != 1.0) {
        ESP_LOGE(TAG, "Unsupported manifest version");
        goto out;
    }
    if (!cJSON_IsString(target) || !cJSON_IsString(fw_version) || !cJSON_IsNumber(image_size) ||
        !cJSON_IsString(image_sha256) || !cJSON_IsString(signature)) {
        ESP_LOGE(TAG, "Manifest missing required fields");
        goto out;
    }

    memset(out, 0, sizeof(*out));
    out->target = parse_target(target->valuestring);
    strncpy(out->version, fw_version->valuestring, sizeof(out->version));
    out->version[sizeof(out->version) - 1] = '\0';
    if (signed_at && cJSON_IsString(signed_at)) {
        strncpy(out->signed_at, signed_at->valuestring, sizeof(out->signed_at));
        out->signed_at[sizeof(out->signed_at) - 1] = '\0';
        out->has_signed_at = true;
    }
    if (image_size->valuedouble < 0.0 || image_size->valuedouble > (double)UINT32_MAX) {
        ESP_LOGE(TAG, "Manifest size out of range");
        goto out;
    }
    out->image_size = (uint32_t)image_size->valuedouble;
    if (!parse_sha256_hex(image_sha256->valuestring, out->image_sha256)) {
        ESP_LOGE(TAG, "Manifest SHA-256 invalid");
        goto out;
    }

    size_t sig_len = strlen(signature->valuestring);
    size_t decoded_len = 0;
    int rc = mbedtls_base64_decode(out->signature, sizeof(out->signature), &decoded_len,
                                   (const unsigned char *)signature->valuestring, sig_len);
    if (rc != 0 || decoded_len != sizeof(out->signature)) {
        ESP_LOGE(TAG, "Manifest signature decode failed (%d, len=%zu)", rc, decoded_len);
        goto out;
    }
    err = ESP_OK;

out:
    cJSON_Delete(root);
    return err;
}

esp_err_t ota_manifest_verify(const ota_manifest_t *manifest)
{
    if (!manifest) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t pubkey[32];
    ESP_RETURN_ON_ERROR(ota_keys_get_pubkey(pubkey), TAG, "public key");

    size_t msg_len = build_signed_message(manifest, NULL, 0);
    if (msg_len == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t *message = (uint8_t *)heap_caps_malloc(msg_len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!message) {
        return ESP_ERR_NO_MEM;
    }
    size_t written = build_signed_message(manifest, message, msg_len + 1);
    if (written != msg_len) {
        free(message);
        return ESP_ERR_INVALID_RESPONSE;
    }
    int ok = crypto_check(manifest->signature, pubkey, message, msg_len);
    free(message);
    if (ok != 0) {
        ESP_LOGE(TAG, "Manifest signature verification failed");
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

bool ota_manifest_is_target(const ota_manifest_t *manifest, ota_target_t target)
{
    if (!manifest) {
        return false;
    }
    return manifest->target == target;
}

const char *ota_manifest_target_name(ota_target_t target)
{
    switch (target) {
    case OTA_TARGET_CONTROLLER:
        return "controller";
    case OTA_TARGET_DOME:
        return "dome";
    default:
        return "unknown";
    }
}

void ota_manifest_sha256_to_hex(const uint8_t digest[32], char out_hex[65])
{
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        out_hex[2 * i] = hex[digest[i] >> 4];
        out_hex[2 * i + 1] = hex[digest[i] & 0x0F];
    }
    out_hex[64] = '\0';
}

static const char *advance_to_digit(const char *s)
{
    while (*s && !isdigit((unsigned char)*s)) {
        ++s;
    }
    return s;
}

static long read_component(const char **s)
{
    const char *p = advance_to_digit(*s);
    if (!*p) {
        *s = p;
        return 0;
    }
    char *end = NULL;
    long value = strtol(p, &end, 10);
    if (end == p) {
        *s = p;
        return 0;
    }
    if (*end == '.') {
        *s = end + 1;
    } else if (*end == '-' || *end == '+') {
        *s = end + 1;
    } else {
        *s = end;
    }
    return value;
}

int ota_manifest_compare_versions(const char *current, const char *candidate)
{
    if (!current || !candidate) {
        return 0;
    }
    const char *cur = current;
    const char *cand = candidate;
    for (int i = 0; i < 4; ++i) {
        long cur_v = read_component(&cur);
        long cand_v = read_component(&cand);
        if (cand_v > cur_v) {
            return 1;
        }
        if (cand_v < cur_v) {
            return -1;
        }
        if ((*cur == '\0' && *cand == '\0') || (!isdigit((unsigned char)*cur) && !isdigit((unsigned char)*cand))) {
            break;
        }
    }
    return 0;
}

