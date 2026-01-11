// OTA update implementation
// Uses ESP-IDF's esp_https_ota for secure firmware updates

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_app_desc.h"
#include "ota.h"

static const char *TAG = "OTA";

// Firmware version (set at compile time)
#define FW_VERSION CONFIG_APP_PROJECT_VER

// OTA state
static OtaStatus s_status = OTA_STATUS_IDLE;
static int s_progress = 0;
static ota_progress_cb_t s_progress_cb = NULL;
static bool s_cancel_requested = false;

// Update status and notify callback
static void update_status(OtaStatus status, int progress)
{
    s_status = status;
    s_progress = progress;
    if (s_progress_cb) {
        s_progress_cb(status, progress);
    }
}

bool ota_init(void)
{
    ESP_LOGI(TAG, "OTA subsystem initialized");
    ESP_LOGI(TAG, "Current firmware: %s", ota_get_version());
    ESP_LOGI(TAG, "Build date: %s", ota_get_build_date());

    // Check if we need to validate the current partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "First boot after OTA, marking as valid");
            ota_mark_valid();
        }
    }

    return true;
}

bool ota_check_update(const char *url, char *version_out, size_t version_len)
{
    if (!url || !version_out || version_len == 0) {
        return false;
    }

    ESP_LOGI(TAG, "Checking for updates at: %s", url);

    // For now, just return false - implement version checking via HTTP header
    // In a real implementation, you would:
    // 1. Make HTTP HEAD request to get version from server
    // 2. Compare with current version
    // 3. Return true if update available

    strncpy(version_out, "Unknown", version_len - 1);
    return false;
}

// HTTP event handler for progress tracking
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            update_status(OTA_STATUS_CONNECTING, 0);
            break;
        case HTTP_EVENT_ON_DATA:
            // Progress is handled by esp_https_ota
            break;
        default:
            break;
    }
    return ESP_OK;
}

// OTA task
static void ota_task(void *arg)
{
    const char *url = (const char *)arg;

    ESP_LOGI(TAG, "Starting OTA from: %s", url);
    update_status(OTA_STATUS_CONNECTING, 0);

    esp_http_client_config_t http_config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        update_status(OTA_STATUS_ERROR_CONNECT, 0);
        vTaskDelete(NULL);
        return;
    }

    update_status(OTA_STATUS_DOWNLOADING, 0);

    // Get image size for progress calculation
    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    int bytes_read = 0;

    while (!s_cancel_requested) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Update progress
        bytes_read = esp_https_ota_get_image_len_read(https_ota_handle);
        if (image_size > 0) {
            int progress = (bytes_read * 100) / image_size;
            update_status(OTA_STATUS_DOWNLOADING, progress);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_cancel_requested) {
        ESP_LOGW(TAG, "OTA cancelled by user");
        esp_https_ota_abort(https_ota_handle);
        update_status(OTA_STATUS_IDLE, 0);
        s_cancel_requested = false;
        vTaskDelete(NULL);
        return;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        update_status(OTA_STATUS_ERROR_DOWNLOAD, 0);
        vTaskDelete(NULL);
        return;
    }

    // Verify the image
    update_status(OTA_STATUS_VERIFYING, 95);

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "Incomplete data received");
        esp_https_ota_abort(https_ota_handle);
        update_status(OTA_STATUS_ERROR_VERIFY, 0);
        vTaskDelete(NULL);
        return;
    }

    // Finish OTA
    update_status(OTA_STATUS_APPLYING, 98);

    err = esp_https_ota_finish(https_ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed");
            update_status(OTA_STATUS_ERROR_VERIFY, 0);
        } else {
            ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
            update_status(OTA_STATUS_ERROR_WRITE, 0);
        }
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA successful! Restarting...");
    update_status(OTA_STATUS_SUCCESS, 100);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool ota_start_update(const char *url, ota_progress_cb_t progress_cb)
{
    if (!url) {
        return false;
    }

    if (s_status == OTA_STATUS_DOWNLOADING || s_status == OTA_STATUS_APPLYING) {
        ESP_LOGW(TAG, "OTA already in progress");
        return false;
    }

    s_progress_cb = progress_cb;
    s_cancel_requested = false;

    // Create OTA task
    BaseType_t ret = xTaskCreate(ota_task, "ota_task", 8192, (void *)url, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return false;
    }

    return true;
}

OtaStatus ota_get_status(void)
{
    return s_status;
}

int ota_get_progress(void)
{
    return s_progress;
}

void ota_cancel(void)
{
    s_cancel_requested = true;
}

void ota_mark_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Firmware marked as valid");
    }
}

bool ota_rollback(void)
{
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(err));
        return false;
    }
    // Device will reboot, this line won't be reached
    return true;
}

const char* ota_get_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

const char* ota_get_build_date(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    static char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%s %s", app_desc->date, app_desc->time);
    return date_buf;
}
