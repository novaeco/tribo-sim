#pragma once

#include "esp_err.h"
#include "app_config.h"
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_init(app_config_t *config);
void ui_show_error(esp_err_t err, const char *message);

#ifdef __cplusplus
}
#endif
