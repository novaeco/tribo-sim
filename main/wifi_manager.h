/**
 * @file wifi_manager.h
 * @brief WiFi management module for ESP-Hosted (ESP32-C6 co-processor)
 * @version 1.0
 * @date 2026-01-08
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>
#include <stdint.h>

// ====================================================================================
// PUBLIC TYPES
// ====================================================================================

#define WIFI_SCAN_MAX_AP 20

// ====================================================================================
// PUBLIC API
// ====================================================================================

/**
 * @brief Initialize WiFi subsystem (via ESP-Hosted)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Start WiFi
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop WiFi
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Check if WiFi is enabled
 */
bool wifi_manager_is_enabled(void);

/**
 * @brief Check if WiFi is connected
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get current WiFi SSID
 * @return Pointer to SSID string (empty if not connected)
 */
const char *wifi_manager_get_ssid(void);

/**
 * @brief Get current IP address
 * @return Pointer to IP string (0.0.0.0 if not connected)
 */
const char *wifi_manager_get_ip(void);

/**
 * @brief Scan for available WiFi networks
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_scan(void);

/**
 * @brief Get scan results
 * @param[out] results Pointer to array of scan results
 * @param[out] count Number of results
 */
void wifi_manager_get_scan_results(wifi_ap_record_t **results, uint16_t *count);

/**
 * @brief Connect to a WiFi network
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect from current network
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Save WiFi credentials to NVS
 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Load WiFi credentials from NVS
 */
esp_err_t wifi_manager_load_credentials(char *ssid, size_t ssid_len,
                                        char *password, size_t pass_len);

/**
 * @brief Check if credentials are saved
 */
bool wifi_manager_has_saved_credentials(void);

/**
 * @brief Delete saved credentials
 */
esp_err_t wifi_manager_delete_credentials(void);

/**
 * @brief Set the currently selected SSID for connection
 */
void wifi_manager_set_selected_ssid(const char *ssid);

/**
 * @brief Get the currently selected SSID
 */
const char *wifi_manager_get_selected_ssid(void);

/**
 * @brief Set the password input
 */
void wifi_manager_set_password_input(const char *password);

/**
 * @brief Get the password input
 */
const char *wifi_manager_get_password_input(void);

#endif // WIFI_MANAGER_H
