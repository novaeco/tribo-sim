#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_H_RES 1024
#define PANEL_V_RES 600

esp_err_t display_driver_init(void);

#ifdef __cplusplus
}
#endif
