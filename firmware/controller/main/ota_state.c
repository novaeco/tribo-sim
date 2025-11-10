#include "ota_state.h"

#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ota_state";

#define OTA_STATE_MAGIC 0x4F544131u

typedef struct {
    uint32_t magic;
    uint8_t state;
    uint8_t reserved[3];
    uint32_t image_size;
    uint8_t sha256[32];
    char version[OTA_MANIFEST_MAX_VERSION_LEN];
    char message[96];
    uint64_t updated_time_us;
} ota_state_blob_t;

static const char *k_namespace = "ota";

static const char *component_key(ota_target_t target)
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

static const char *state_to_string(ota_state_code_t state)
{
    switch (state) {
    case OTA_STATE_IDLE:
        return "idle";
    case OTA_STATE_MANIFEST_ACCEPTED:
        return "manifest";
    case OTA_STATE_DOWNLOADING:
        return "downloading";
    case OTA_STATE_VERIFYING:
        return "verifying";
    case OTA_STATE_READY:
        return "ready";
    case OTA_STATE_PENDING_REBOOT:
        return "pending_reboot";
    case OTA_STATE_SUCCESS:
        return "success";
    case OTA_STATE_FAILED:
        return "failed";
    case OTA_STATE_ROLLED_BACK:
        return "rolled_back";
    default:
        return "unknown";
    }
}

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static esp_err_t load_blob(ota_target_t target, ota_state_blob_t *blob)
{
    if (!blob) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t required = sizeof(*blob);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_blob(handle, component_key(target), blob, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (required != sizeof(*blob) || blob->magic != OTA_STATE_MAGIC) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t store_blob(ota_target_t target, const ota_state_blob_t *blob)
{
    if (!blob) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(k_namespace, NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(handle, component_key(target), blob, sizeof(*blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t ota_state_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_close(handle);
    }
    return err;
}

static ota_state_blob_t blob_from_manifest(const ota_manifest_t *manifest)
{
    ota_state_blob_t blob = {
        .magic = OTA_STATE_MAGIC,
        .state = OTA_STATE_MANIFEST_ACCEPTED,
        .image_size = manifest ? manifest->image_size : 0,
        .updated_time_us = now_us(),
    };
    if (manifest) {
        memcpy(blob.sha256, manifest->image_sha256, sizeof(blob.sha256));
        strncpy(blob.version, manifest->version, sizeof(blob.version));
        blob.version[sizeof(blob.version) - 1] = '\0';
    }
    strncpy(blob.message, "Manifest accepté", sizeof(blob.message));
    blob.message[sizeof(blob.message) - 1] = '\0';
    return blob;
}

static esp_err_t update_message(ota_state_blob_t *blob, ota_state_code_t state, const char *message)
{
    if (!blob) {
        return ESP_ERR_INVALID_ARG;
    }
    blob->state = (uint8_t)state;
    blob->updated_time_us = now_us();
    if (message && message[0]) {
        strncpy(blob->message, message, sizeof(blob->message));
    } else {
        strncpy(blob->message, state_to_string(state), sizeof(blob->message));
    }
    blob->message[sizeof(blob->message) - 1] = '\0';
    return ESP_OK;
}

esp_err_t ota_state_begin(ota_target_t target, const ota_manifest_t *manifest, const char *message)
{
    if (!manifest) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_state_blob_t blob = blob_from_manifest(manifest);
    if (message) {
        strncpy(blob.message, message, sizeof(blob.message));
        blob.message[sizeof(blob.message) - 1] = '\0';
    }
    ESP_RETURN_ON_ERROR(store_blob(target, &blob), TAG, "store begin");
    ESP_LOGI(TAG, "%s manifest recorded (%s)", component_key(target), blob.version);
    return ESP_OK;
}

esp_err_t ota_state_transition(ota_target_t target, ota_state_code_t new_state, const char *message)
{
    ota_state_blob_t blob = {0};
    esp_err_t err = load_blob(target, &blob);
    if (err != ESP_OK) {
        memset(&blob, 0, sizeof(blob));
        blob.magic = OTA_STATE_MAGIC;
        blob.state = OTA_STATE_IDLE;
    }
    update_message(&blob, new_state, message);
    ESP_RETURN_ON_ERROR(store_blob(target, &blob), TAG, "store transition");
    ESP_LOGI(TAG, "%s OTA -> %s", component_key(target), state_to_string(new_state));
    return ESP_OK;
}

esp_err_t ota_state_fail(ota_target_t target, const char *message)
{
    return ota_state_transition(target, OTA_STATE_FAILED, message);
}

esp_err_t ota_state_get(ota_target_t target, ota_status_entry_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_state_blob_t blob = {0};
    esp_err_t err = load_blob(target, &blob);
    if (err != ESP_OK) {
        return err;
    }
    memset(out, 0, sizeof(*out));
    out->state = (ota_state_code_t)blob.state;
    out->image_size = blob.image_size;
    memcpy(out->sha256, blob.sha256, sizeof(out->sha256));
    strncpy(out->version, blob.version, sizeof(out->version));
    strncpy(out->message, blob.message, sizeof(out->message));
    out->version[sizeof(out->version) - 1] = '\0';
    out->message[sizeof(out->message) - 1] = '\0';
    out->updated_time_us = blob.updated_time_us;
    return ESP_OK;
}

void ota_state_append_status_json(cJSON *root)
{
    if (!root) {
        return;
    }
    cJSON *ota = cJSON_AddObjectToObject(root, "ota");
    if (!ota) {
        return;
    }
    for (ota_target_t target = OTA_TARGET_CONTROLLER; target <= OTA_TARGET_DOME; ++target) {
        ota_status_entry_t entry = {0};
        cJSON *obj = cJSON_AddObjectToObject(ota, component_key(target));
        if (!obj) {
            continue;
        }
        if (ota_state_get(target, &entry) != ESP_OK) {
            cJSON_AddStringToObject(obj, "state", "unknown");
            continue;
        }
        cJSON_AddStringToObject(obj, "state", state_to_string(entry.state));
        cJSON_AddStringToObject(obj, "message", entry.message);
        cJSON_AddStringToObject(obj, "version", entry.version);
        cJSON_AddNumberToObject(obj, "image_size", (double)entry.image_size);
        char sha_hex[65];
        ota_manifest_sha256_to_hex(entry.sha256, sha_hex);
        cJSON_AddStringToObject(obj, "sha256", sha_hex);
        cJSON_AddNumberToObject(obj, "updated_us", (double)entry.updated_time_us);
    }
}

esp_err_t ota_state_on_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        return ESP_FAIL;
    }
    esp_app_desc_t desc = {0};
    ESP_RETURN_ON_ERROR(esp_ota_get_partition_description(running, &desc), TAG, "get desc");

    ota_status_entry_t entry = {0};
    if (ota_state_get(OTA_TARGET_CONTROLLER, &entry) == ESP_OK) {
        if (strlen(entry.version) > 0 && strcmp(entry.version, desc.version) != 0) {
            ESP_LOGW(TAG, "Detected rollback to %s (expected %s)", desc.version, entry.version);
            ota_state_transition(OTA_TARGET_CONTROLLER, OTA_STATE_ROLLED_BACK, "Rollback vers précédent");
        } else {
            esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
            esp_err_t st_err = esp_ota_get_state_partition(running, &state);
            if (st_err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
                ota_state_transition(OTA_TARGET_CONTROLLER, OTA_STATE_VERIFYING, "Auto-test en cours");
            }
        }
    }
    return ESP_OK;
}

esp_err_t ota_state_mark_running_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_app_desc_t desc = {0};
    ESP_RETURN_ON_ERROR(esp_ota_get_partition_description(running, &desc), TAG, "get desc");

    ota_status_entry_t entry = {0};
    if (ota_state_get(OTA_TARGET_CONTROLLER, &entry) != ESP_OK) {
        return ESP_OK;
    }
    if (strcmp(entry.version, desc.version) != 0) {
        return ESP_OK;
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK && err != ESP_ERR_OTA_APP_INVALID) {
        ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
        return err;
    }
    return ota_state_transition(OTA_TARGET_CONTROLLER, OTA_STATE_SUCCESS, "OTA validée");
}

