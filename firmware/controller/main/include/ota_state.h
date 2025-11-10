#pragma once

#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#include "ota_manifest.h"

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_MANIFEST_ACCEPTED = 1,
    OTA_STATE_DOWNLOADING = 2,
    OTA_STATE_VERIFYING = 3,
    OTA_STATE_READY = 4,
    OTA_STATE_PENDING_REBOOT = 5,
    OTA_STATE_SUCCESS = 6,
    OTA_STATE_FAILED = 7,
    OTA_STATE_ROLLED_BACK = 8,
} ota_state_code_t;

typedef struct {
    ota_state_code_t state;
    uint32_t image_size;
    uint8_t sha256[32];
    char version[OTA_MANIFEST_MAX_VERSION_LEN];
    char message[96];
    uint64_t updated_time_us;
} ota_status_entry_t;

esp_err_t ota_state_init(void);

esp_err_t ota_state_begin(ota_target_t target, const ota_manifest_t *manifest, const char *message);

esp_err_t ota_state_transition(ota_target_t target, ota_state_code_t new_state, const char *message);

esp_err_t ota_state_fail(ota_target_t target, const char *message);

esp_err_t ota_state_get(ota_target_t target, ota_status_entry_t *out);

void ota_state_append_status_json(cJSON *root);

esp_err_t ota_state_on_boot(void);

esp_err_t ota_state_mark_running_valid(void);

