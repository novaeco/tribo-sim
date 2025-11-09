#include "lvgl_port.h"

#include "display_driver.h"
#include "touch_gt911.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_check.h"

static const char *TAG = "lvgl";

static SemaphoreHandle_t s_lvgl_mutex;
static esp_lcd_panel_handle_t s_panel_handle;
static lv_disp_t *s_disp;
static lv_indev_t *s_touch;
static esp_timer_handle_t s_tick_timer;

#define LVGL_DRAW_BUF_HEIGHT   60
#define LVGL_BUFFER_PIXELS     (PANEL_H_RES * LVGL_DRAW_BUF_HEIGHT)

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(10);
}

static void lvgl_port_task(void *arg)
{
    (void)arg;
    while (1) {
        lvgl_port_lock();
        lv_timer_handler();
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void lvgl_port_lock(void)
{
    if (s_lvgl_mutex) {
        xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
    }
}

void lvgl_port_unlock(void)
{
    if (s_lvgl_mutex) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

static void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_map)
{
    const int32_t x1 = area->x1;
    const int32_t y1 = area->y1;
    const int32_t x2 = area->x2 + 1;
    const int32_t y2 = area->y2 + 1;
    esp_lcd_panel_draw_bitmap(s_panel_handle, x1, y1, x2, y2, color_map);
    lv_disp_flush_ready(disp_drv);
}

void *lvgl_port_malloc(size_t size)
{
    return heap_caps_aligned_alloc(32, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void lvgl_port_free(void *ptr)
{
    if (ptr) {
        heap_caps_free(ptr);
    }
}

lv_disp_t *lvgl_port_get_display(void)
{
    return s_disp;
}

lv_indev_t *lvgl_port_get_touch_indev(void)
{
    return s_touch;
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel_handle)
{
    s_panel_handle = panel_handle;
    lv_init();

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    const size_t buf_size = LVGL_BUFFER_PIXELS * sizeof(lv_color_t);
    s_buf1 = (lv_color_t *)lvgl_port_malloc(buf_size);
    s_buf2 = (lv_color_t *)lvgl_port_malloc(buf_size);
    if (!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "Failed to allocate draw buffers");
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, LVGL_BUFFER_PIXELS);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = PANEL_H_RES;
    disp_drv.ver_res = PANEL_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    disp_drv.color_format = LV_COLOR_FORMAT_NATIVE;
    disp_drv.antialiasing = 1;
    s_disp = lv_disp_drv_register(&disp_drv);

    esp_err_t err = touch_gt911_init(&s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lv_tick"
    };
    err = esp_timer_create(&timer_args, &s_tick_timer);
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to create LVGL tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_tick_timer, 10 * 1000), TAG, "Failed to start LVGL tick timer");

    xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", 4096, NULL, 5, NULL, 1);

    return ESP_OK;
}
