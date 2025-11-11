#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_wifi_types.h"

esp_err_t wifi_start_apsta(const char *ssid, const char *pass);
esp_err_t wifi_set_sta_config(const wifi_sta_config_t *cfg, bool persist);
esp_err_t wifi_load_sta_config(void);
bool wifi_has_sta_config(void);
