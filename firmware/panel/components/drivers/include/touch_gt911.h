#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*touch_gt911_event_cb_t)(const lv_indev_data_t *data, void *user_data);

esp_err_t touch_gt911_init(lv_indev_t **indev_out);

#ifdef __cplusplus
}
#endif
