// OTA (Over-The-Air) update subsystem
// Supports firmware updates via HTTP/HTTPS

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OTA status codes
typedef enum {
    OTA_STATUS_IDLE = 0,
    OTA_STATUS_CONNECTING,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_VERIFYING,
    OTA_STATUS_APPLYING,
    OTA_STATUS_SUCCESS,
    OTA_STATUS_ERROR_CONNECT,
    OTA_STATUS_ERROR_DOWNLOAD,
    OTA_STATUS_ERROR_VERIFY,
    OTA_STATUS_ERROR_WRITE,
    OTA_STATUS_ERROR_NO_WIFI,
} OtaStatus;

// OTA progress callback
typedef void (*ota_progress_cb_t)(OtaStatus status, int progress_percent);

// Initialize OTA subsystem
bool ota_init(void);

// Check for available updates
// Returns true if update is available
bool ota_check_update(const char *url, char *version_out, size_t version_len);

// Start OTA update from URL
// progress_cb is called with status updates (can be NULL)
// Returns true if update started successfully
bool ota_start_update(const char *url, ota_progress_cb_t progress_cb);

// Get current OTA status
OtaStatus ota_get_status(void);

// Get current progress (0-100)
int ota_get_progress(void);

// Cancel ongoing update
void ota_cancel(void);

// Mark current firmware as valid (call after successful boot)
void ota_mark_valid(void);

// Rollback to previous firmware
bool ota_rollback(void);

// Get current firmware version
const char* ota_get_version(void);

// Get firmware build date
const char* ota_get_build_date(void);

#ifdef __cplusplus
}
#endif
