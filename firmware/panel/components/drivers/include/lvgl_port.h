#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel_handle);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);

lv_disp_t *lvgl_port_get_display(void);
lv_indev_t *lvgl_port_get_touch_indev(void);

void *lvgl_port_malloc(size_t size);
void lvgl_port_free(void *ptr);

#ifdef __cplusplus
}
#endif

#define LV_MEM_CUSTOM_ALLOC   lvgl_port_malloc
#define LV_MEM_CUSTOM_FREE    lvgl_port_free
