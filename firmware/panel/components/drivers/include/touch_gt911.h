#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_gt911_init(lv_disp_t *disp, lv_indev_t **indev_out);

#ifdef __cplusplus
}
#endif
