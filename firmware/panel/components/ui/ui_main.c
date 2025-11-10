#include "ui_main.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "lvgl_port.h"

#include "app_config.h"
#include "display_driver.h"
#include "localization.h"
#include "network_manager.h"

#define TAG "ui"

#define HISTORY_CAPACITY       1440
#define CHART_VISIBLE_POINTS   240
#define OTA_ROLE_CONTROLLER    0
#define OTA_ROLE_DOME          1

typedef enum {
    SLIDER_CCT_DAY = 0,
    SLIDER_CCT_WARM,
    SLIDER_UVA_SET,
    SLIDER_UVA_CLAMP,
    SLIDER_UVB_SET,
    SLIDER_UVB_CLAMP,
    SLIDER_COUNT
} slider_type_t;

typedef struct {
    slider_type_t type;
    lv_obj_t *slider;
    lv_obj_t *value_label;
} slider_binding_t;

typedef struct {
    lv_obj_t *label;
    ui_string_id_t id;
} localized_label_t;

typedef struct {
    app_config_t *config;
    ui_language_t language;
    slider_binding_t sliders[SLIDER_COUNT];
    lv_obj_t *tabview;
    lv_obj_t *label_status_banner;
    lv_obj_t *label_summary;
    lv_obj_t *label_sensor_sht31;
    lv_obj_t *label_sensor_sht21;
    lv_obj_t *label_sensor_bme280;
    lv_obj_t *label_sensor_ds18b20;
    lv_obj_t *label_sensor_ambient;
    lv_obj_t *label_dome_state;
    lv_obj_t *label_interlock;
    lv_obj_t *led_interlock;
    lv_obj_t *btn_mute;
    lv_obj_t *btn_mute_label;
    lv_obj_t *dropdown_sky;
    lv_obj_t *dropdown_language;
    lv_obj_t *dropdown_species;
    lv_obj_t *btn_apply_species;
    lv_obj_t *btn_refresh_species;
    lv_obj_t *txt_controller_path;
    lv_obj_t *txt_dome_path;
    lv_obj_t *spin_uvb_period;
    lv_obj_t *spin_uvb_duty;
    lv_obj_t *spin_calib_k;
    lv_obj_t *spin_calib_uvi;
    lv_obj_t *label_calib_status;
    lv_obj_t *ta_ssid;
    lv_obj_t *ta_password;
    lv_obj_t *ta_host;
    lv_obj_t *spin_port;
    lv_obj_t *sw_tls;
    lv_obj_t *chart;
    lv_chart_series_t *chart_temp;
    lv_chart_series_t *chart_hum;
    lv_chart_series_t *chart_uvi;
    float *history_temp;
    float *history_hum;
    float *history_uvi;
    size_t history_capacity;
    size_t history_count;
    size_t history_head;
    terrarium_status_t last_status;
    terrarium_species_catalog_t species_catalog;
    const terrarium_species_entry_t *species_option_entries[NETWORK_SPECIES_MAX_ENTRIES];
    size_t species_option_count;
    bool alarm_muted;
    bool updating_controls;
    localized_label_t localized_labels[64];
    size_t localized_label_count;
    uint16_t tab_dashboard_idx;
    uint16_t tab_control_idx;
    uint16_t tab_settings_idx;
} ui_context_t;

typedef struct {
    bool error;
    char text[160];
} status_banner_msg_t;

static ui_context_t s_ctx = {0};

static lv_obj_t *create_sensor_row(lv_obj_t *parent, ui_string_id_t title_id, lv_obj_t **value_label);
static lv_obj_t *create_slider_card(lv_obj_t *parent, ui_string_id_t title_id, slider_type_t type, slider_binding_t *binding);
static lv_obj_t *create_spinbox(lv_obj_t *parent, ui_string_id_t title_id, int32_t min, int32_t max, uint32_t step, lv_obj_t **out);
static lv_obj_t *create_switch_row(lv_obj_t *parent, ui_string_id_t title_id, lv_obj_t **out_switch);
static void register_localized_label(lv_obj_t *label, ui_string_id_t id);
static void update_all_localized_labels(void);
static void update_tab_titles(void);

static void slider_event_cb(lv_event_t *e);
static void spinbox_event_cb(lv_event_t *e);
static void mute_btn_event_cb(lv_event_t *e);
static void sky_dropdown_event_cb(lv_event_t *e);
static void language_dropdown_event_cb(lv_event_t *e);
static void species_apply_event_cb(lv_event_t *e);
static void species_refresh_event_cb(lv_event_t *e);
static void ota_upload_event_cb(lv_event_t *e);
static void calibration_fetch_event_cb(lv_event_t *e);
static void calibration_apply_event_cb(lv_event_t *e);
static void settings_save_event_cb(lv_event_t *e);

static void network_status_cb(const terrarium_status_t *status, void *ctx);
static void network_error_cb(esp_err_t err, const char *message, void *ctx);
static void network_species_cb(const terrarium_species_catalog_t *catalog, void *ctx);
static void ui_apply_status_async(void *param);
static void ui_apply_species_async(void *param);

static void apply_status_to_widgets(const terrarium_status_t *status);
static void update_slider_label(slider_binding_t *binding, int32_t value);
static void set_status_banner_locked(const char *text, bool error);
static void set_status_banner(const char *text, bool error);
static void notify_error(ui_string_id_t prefix_id, esp_err_t err);
static void send_light_command(void);
static void update_alarm_button(void);
static void populate_settings_form(void);
static void store_history_sample(float temp, float hum, float uvi);
static void refresh_chart(void);
static void apply_species_to_dropdown(void);

static void status_banner_async(void *param);

esp_err_t ui_init(app_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.config = config;
    s_ctx.language = ui_loc_from_code(config->language);
    s_ctx.history_capacity = HISTORY_CAPACITY;
    s_ctx.history_temp = heap_caps_malloc(sizeof(float) * HISTORY_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ctx.history_hum = heap_caps_malloc(sizeof(float) * HISTORY_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ctx.history_uvi = heap_caps_malloc(sizeof(float) * HISTORY_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ctx.history_temp || !s_ctx.history_hum || !s_ctx.history_uvi) {
        ESP_LOGW(TAG, "History buffers using DRAM fallback");
        if (!s_ctx.history_temp) {
            s_ctx.history_temp = calloc(HISTORY_CAPACITY, sizeof(float));
        }
        if (!s_ctx.history_hum) {
            s_ctx.history_hum = calloc(HISTORY_CAPACITY, sizeof(float));
        }
        if (!s_ctx.history_uvi) {
            s_ctx.history_uvi = calloc(HISTORY_CAPACITY, sizeof(float));
        }
    }
    if (!s_ctx.history_temp || !s_ctx.history_hum || !s_ctx.history_uvi) {
        return ESP_ERR_NO_MEM;
    }

    lvgl_port_lock();

    lv_obj_t *scr = lv_disp_get_scr_act(lvgl_port_get_display());
    lv_obj_set_style_pad_all(scr, 8, 0);
    s_ctx.tabview = lv_tabview_create(scr, LV_DIR_TOP, 48);

    lv_obj_t *tab_dashboard = lv_tabview_add_tab(s_ctx.tabview, ui_loc_get(s_ctx.language, UI_STR_TAB_DASHBOARD));
    lv_obj_t *tab_control = lv_tabview_add_tab(s_ctx.tabview, ui_loc_get(s_ctx.language, UI_STR_TAB_CONTROL));
    lv_obj_t *tab_settings = lv_tabview_add_tab(s_ctx.tabview, ui_loc_get(s_ctx.language, UI_STR_TAB_SETTINGS));
    s_ctx.tab_dashboard_idx = 0;
    s_ctx.tab_control_idx = 1;
    s_ctx.tab_settings_idx = 2;

    /* Dashboard */
    lv_obj_set_flex_flow(tab_dashboard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_dashboard, 12, 0);

    s_ctx.label_status_banner = lv_label_create(tab_dashboard);
    lv_label_set_text(s_ctx.label_status_banner, ui_loc_get(s_ctx.language, UI_STR_STATUS_CONNECTING));
    lv_obj_set_style_text_color(s_ctx.label_status_banner, lv_palette_main(LV_PALETTE_BLUE), 0);

    s_ctx.label_summary = lv_label_create(tab_dashboard);
    lv_label_set_long_mode(s_ctx.label_summary, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_ctx.label_summary, "");

    lv_obj_t *sensor_card = lv_obj_create(tab_dashboard);
    lv_obj_set_size(sensor_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(sensor_card, 12, 0);
    lv_obj_set_style_bg_opa(sensor_card, LV_OPA_20, 0);
    lv_obj_set_style_radius(sensor_card, 12, 0);
    lv_obj_set_flex_flow(sensor_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sensor_card, 6, 0);

    register_localized_label(lv_label_create(sensor_card), UI_STR_SENSOR_SECTION);
    create_sensor_row(sensor_card, UI_STR_SENSOR_SHT31, &s_ctx.label_sensor_sht31);
    create_sensor_row(sensor_card, UI_STR_SENSOR_SHT21, &s_ctx.label_sensor_sht21);
    create_sensor_row(sensor_card, UI_STR_SENSOR_BME280, &s_ctx.label_sensor_bme280);
    create_sensor_row(sensor_card, UI_STR_SENSOR_DS18B20, &s_ctx.label_sensor_ds18b20);
    create_sensor_row(sensor_card, UI_STR_SENSOR_AMBIENT, &s_ctx.label_sensor_ambient);

    lv_obj_t *dome_card = lv_obj_create(tab_dashboard);
    lv_obj_set_size(dome_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(dome_card, 12, 0);
    lv_obj_set_style_radius(dome_card, 12, 0);
    lv_obj_set_style_bg_opa(dome_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(dome_card, LV_FLEX_FLOW_COLUMN);
    register_localized_label(lv_label_create(dome_card), UI_STR_DOME_SECTION);
    s_ctx.label_dome_state = lv_label_create(dome_card);
    lv_label_set_text(s_ctx.label_dome_state, "--");

    lv_obj_t *interlock_row = lv_obj_create(dome_card);
    lv_obj_set_style_bg_opa(interlock_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(interlock_row, 0, 0);
    lv_obj_set_flex_flow(interlock_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(interlock_row, 4, 0);
    s_ctx.led_interlock = lv_led_create(interlock_row);
    lv_led_off(s_ctx.led_interlock);
    s_ctx.label_interlock = lv_label_create(interlock_row);
    register_localized_label(s_ctx.label_interlock, UI_STR_INTERLOCK_OK);

    s_ctx.btn_mute = lv_btn_create(dome_card);
    lv_obj_add_event_cb(s_ctx.btn_mute, mute_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_ctx.btn_mute_label = lv_label_create(s_ctx.btn_mute);
    register_localized_label(s_ctx.btn_mute_label, UI_STR_ALARM_MUTE);

    lv_obj_t *chart_card = lv_obj_create(tab_dashboard);
    lv_obj_set_size(chart_card, LV_PCT(100), 240);
    lv_obj_set_style_pad_all(chart_card, 12, 0);
    lv_obj_set_style_radius(chart_card, 12, 0);
    lv_obj_set_style_bg_opa(chart_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(chart_card, LV_FLEX_FLOW_COLUMN);
    register_localized_label(lv_label_create(chart_card), UI_STR_TELEMETRY_SECTION);

    s_ctx.chart = lv_chart_create(chart_card);
    lv_obj_set_size(s_ctx.chart, LV_PCT(100), LV_PCT(100));
    lv_chart_set_type(s_ctx.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ctx.chart, CHART_VISIBLE_POINTS);
    lv_chart_set_range(s_ctx.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(s_ctx.chart, 6, 4);
    s_ctx.chart_temp = lv_chart_add_series(s_ctx.chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.chart_hum = lv_chart_add_series(s_ctx.chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.chart_uvi = lv_chart_add_series(s_ctx.chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *ota_card = lv_obj_create(tab_dashboard);
    lv_obj_set_size(ota_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ota_card, 12, 0);
    lv_obj_set_style_radius(ota_card, 12, 0);
    lv_obj_set_style_bg_opa(ota_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(ota_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ota_card, 8, 0);
    register_localized_label(lv_label_create(ota_card), UI_STR_OTA_SECTION);

    lv_obj_t *ctrl_row = lv_obj_create(ota_card);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    register_localized_label(lv_label_create(ctrl_row), UI_STR_OTA_CONTROLLER_PATH);
    s_ctx.txt_controller_path = lv_textarea_create(ctrl_row);
    lv_textarea_set_one_line(s_ctx.txt_controller_path, true);
    lv_textarea_set_placeholder_text(s_ctx.txt_controller_path, "/sdcard/controller.bin");
    lv_obj_t *btn_ctrl = lv_btn_create(ctrl_row);
    lv_obj_add_event_cb(btn_ctrl, ota_upload_event_cb, LV_EVENT_CLICKED, (void *)OTA_ROLE_CONTROLLER);
    register_localized_label(lv_label_create(btn_ctrl), UI_STR_OTA_CONTROLLER_UPLOAD);

    lv_obj_t *dome_row = lv_obj_create(ota_card);
    lv_obj_set_style_bg_opa(dome_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dome_row, 0, 0);
    lv_obj_set_flex_flow(dome_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(dome_row, 0, 0);
    register_localized_label(lv_label_create(dome_row), UI_STR_OTA_DOME_PATH);
    s_ctx.txt_dome_path = lv_textarea_create(dome_row);
    lv_textarea_set_one_line(s_ctx.txt_dome_path, true);
    lv_textarea_set_placeholder_text(s_ctx.txt_dome_path, "/sdcard/dome.bin");
    lv_obj_t *btn_dome = lv_btn_create(dome_row);
    lv_obj_add_event_cb(btn_dome, ota_upload_event_cb, LV_EVENT_CLICKED, (void *)OTA_ROLE_DOME);
    register_localized_label(lv_label_create(btn_dome), UI_STR_OTA_DOME_UPLOAD);

    /* Control tab */
    lv_obj_set_flex_flow(tab_control, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_control, 12, 0);

    lv_obj_t *species_card = lv_obj_create(tab_control);
    lv_obj_set_size(species_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(species_card, 12, 0);
    lv_obj_set_style_radius(species_card, 12, 0);
    lv_obj_set_style_bg_opa(species_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(species_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(species_card, 8, 0);
    register_localized_label(lv_label_create(species_card), UI_STR_SPECIES_SECTION);

    s_ctx.dropdown_species = lv_dropdown_create(species_card);
    lv_obj_set_width(s_ctx.dropdown_species, LV_PCT(100));
    lv_dropdown_set_options(s_ctx.dropdown_species, "");

    lv_obj_t *species_btn_row = lv_obj_create(species_card);
    lv_obj_set_style_bg_opa(species_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(species_btn_row, 0, 0);
    lv_obj_set_flex_flow(species_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(species_btn_row, 8, 0);

    s_ctx.btn_apply_species = lv_btn_create(species_btn_row);
    lv_obj_add_event_cb(s_ctx.btn_apply_species, species_apply_event_cb, LV_EVENT_CLICKED, NULL);
    register_localized_label(lv_label_create(s_ctx.btn_apply_species), UI_STR_SPECIES_APPLY);

    s_ctx.btn_refresh_species = lv_btn_create(species_btn_row);
    lv_obj_add_event_cb(s_ctx.btn_refresh_species, species_refresh_event_cb, LV_EVENT_CLICKED, NULL);
    register_localized_label(lv_label_create(s_ctx.btn_refresh_species), UI_STR_SPECIES_REFRESH);

    lv_obj_t *sliders_card = lv_obj_create(tab_control);
    lv_obj_set_size(sliders_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(sliders_card, 12, 0);
    lv_obj_set_style_radius(sliders_card, 12, 0);
    lv_obj_set_style_bg_opa(sliders_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(sliders_card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(sliders_card, 12, 0);
    lv_obj_set_style_pad_row(sliders_card, 12, 0);

    create_slider_card(sliders_card, UI_STR_LIGHT_CCT_DAY, SLIDER_CCT_DAY, &s_ctx.sliders[SLIDER_CCT_DAY]);
    create_slider_card(sliders_card, UI_STR_LIGHT_CCT_WARM, SLIDER_CCT_WARM, &s_ctx.sliders[SLIDER_CCT_WARM]);
    create_slider_card(sliders_card, UI_STR_LIGHT_UVA_SET, SLIDER_UVA_SET, &s_ctx.sliders[SLIDER_UVA_SET]);
    create_slider_card(sliders_card, UI_STR_LIGHT_UVA_CLAMP, SLIDER_UVA_CLAMP, &s_ctx.sliders[SLIDER_UVA_CLAMP]);
    create_slider_card(sliders_card, UI_STR_LIGHT_UVB_SET, SLIDER_UVB_SET, &s_ctx.sliders[SLIDER_UVB_SET]);
    create_slider_card(sliders_card, UI_STR_LIGHT_UVB_CLAMP, SLIDER_UVB_CLAMP, &s_ctx.sliders[SLIDER_UVB_CLAMP]);

    lv_obj_t *uvb_card = lv_obj_create(tab_control);
    lv_obj_set_size(uvb_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(uvb_card, 12, 0);
    lv_obj_set_style_radius(uvb_card, 12, 0);
    lv_obj_set_style_bg_opa(uvb_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(uvb_card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(uvb_card, 8, 0);
    lv_obj_set_style_pad_column(uvb_card, 12, 0);

    create_spinbox(uvb_card, UI_STR_LIGHT_UVB_PERIOD, 1, 3600, 1, &s_ctx.spin_uvb_period);
    lv_obj_add_event_cb(s_ctx.spin_uvb_period, spinbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_spinbox(uvb_card, UI_STR_LIGHT_UVB_DUTY, 0, 1000, 5, &s_ctx.spin_uvb_duty);
    lv_obj_add_event_cb(s_ctx.spin_uvb_duty, spinbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *sky_row = lv_obj_create(tab_control);
    lv_obj_set_style_bg_opa(sky_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sky_row, 0, 0);
    lv_obj_set_flex_flow(sky_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sky_row, 12, 0);
    register_localized_label(lv_label_create(sky_row), UI_STR_LIGHT_SKY);

    s_ctx.dropdown_sky = lv_dropdown_create(sky_row);
    lv_dropdown_set_options_static(s_ctx.dropdown_sky, "0\n1\n2");
    lv_obj_add_event_cb(s_ctx.dropdown_sky, sky_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *apply_btn = lv_btn_create(tab_control);
    lv_obj_add_event_cb(apply_btn, slider_event_cb, LV_EVENT_CLICKED, NULL);
    register_localized_label(lv_label_create(apply_btn), UI_STR_LIGHT_APPLY);

    /* Settings tab */
    lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_settings, 12, 0);

    lv_obj_t *language_card = lv_obj_create(tab_settings);
    lv_obj_set_size(language_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(language_card, 12, 0);
    lv_obj_set_style_radius(language_card, 12, 0);
    lv_obj_set_style_bg_opa(language_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(language_card, LV_FLEX_FLOW_COLUMN);
    register_localized_label(lv_label_create(language_card), UI_STR_LANGUAGE_SELECT);

    s_ctx.dropdown_language = lv_dropdown_create(language_card);
    lv_obj_set_width(s_ctx.dropdown_language, 180);
    lv_dropdown_set_options(s_ctx.dropdown_language, ui_loc_language_options());
    lv_obj_add_event_cb(s_ctx.dropdown_language, language_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *calib_card = lv_obj_create(tab_settings);
    lv_obj_set_size(calib_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(calib_card, 12, 0);
    lv_obj_set_style_radius(calib_card, 12, 0);
    lv_obj_set_style_bg_opa(calib_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(calib_card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(calib_card, 12, 0);
    lv_obj_set_style_pad_row(calib_card, 8, 0);
    register_localized_label(lv_label_create(calib_card), UI_STR_CALIB_SECTION);

    create_spinbox(calib_card, UI_STR_CALIB_K, 0, 10000, 1, &s_ctx.spin_calib_k);
    lv_spinbox_set_digit_format(s_ctx.spin_calib_k, 6, 2);
    create_spinbox(calib_card, UI_STR_CALIB_UVI_MAX, 0, 1000, 1, &s_ctx.spin_calib_uvi);
    lv_spinbox_set_digit_format(s_ctx.spin_calib_uvi, 6, 2);

    lv_obj_t *calib_btn_row = lv_obj_create(tab_settings);
    lv_obj_set_style_bg_opa(calib_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calib_btn_row, 0, 0);
    lv_obj_set_flex_flow(calib_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(calib_btn_row, 12, 0);

    lv_obj_t *btn_fetch = lv_btn_create(calib_btn_row);
    lv_obj_add_event_cb(btn_fetch, calibration_fetch_event_cb, LV_EVENT_CLICKED, NULL);
    register_localized_label(lv_label_create(btn_fetch), UI_STR_CALIB_FETCH);

    lv_obj_t *btn_apply = lv_btn_create(calib_btn_row);
    lv_obj_add_event_cb(btn_apply, calibration_apply_event_cb, LV_EVENT_CLICKED, NULL);
    register_localized_label(lv_label_create(btn_apply), UI_STR_CALIB_APPLY);

    s_ctx.label_calib_status = lv_label_create(tab_settings);
    lv_label_set_text(s_ctx.label_calib_status, "");

    lv_obj_t *network_card = lv_obj_create(tab_settings);
    lv_obj_set_size(network_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(network_card, 12, 0);
    lv_obj_set_style_radius(network_card, 12, 0);
    lv_obj_set_style_bg_opa(network_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(network_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(network_card, 8, 0);
    register_localized_label(lv_label_create(network_card), UI_STR_NETWORK_SECTION);

    register_localized_label(lv_label_create(network_card), UI_STR_NETWORK_SSID);
    s_ctx.ta_ssid = lv_textarea_create(network_card);
    lv_textarea_set_one_line(s_ctx.ta_ssid, true);
    register_localized_label(lv_label_create(network_card), UI_STR_NETWORK_PASSWORD);
    s_ctx.ta_password = lv_textarea_create(network_card);
    lv_textarea_set_one_line(s_ctx.ta_password, true);
    lv_textarea_set_password_mode(s_ctx.ta_password, true);
    register_localized_label(lv_label_create(network_card), UI_STR_NETWORK_HOST);
    s_ctx.ta_host = lv_textarea_create(network_card);
    lv_textarea_set_one_line(s_ctx.ta_host, true);

    create_spinbox(network_card, UI_STR_NETWORK_PORT, 1, 65535, 1, &s_ctx.spin_port);
    create_switch_row(network_card, UI_STR_NETWORK_TLS, &s_ctx.sw_tls);

    lv_obj_t *btn_save = lv_btn_create(network_card);
    lv_obj_add_event_cb(btn_save, settings_save_event_cb, LV_EVENT_CLICKED, NULL);
    register_localized_label(lv_label_create(btn_save), UI_STR_NETWORK_SAVE);

    populate_settings_form();
    update_all_localized_labels();
    update_tab_titles();

    lvgl_port_unlock();

    network_manager_register_status_callback(network_status_cb, NULL);
    network_manager_register_error_callback(network_error_cb, NULL);
    network_manager_register_species_callback(network_species_cb, NULL);
    network_manager_request_species_catalog();

    return ESP_OK;
}

void ui_show_error(esp_err_t err, const char *message)
{
    lvgl_port_lock();
    char buf[160];
    snprintf(buf, sizeof(buf), "%s (%s)", message ? message : "Error", esp_err_to_name(err));
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Error", buf, NULL, true);
    lv_obj_center(mbox);
    lvgl_port_unlock();
}

static lv_obj_t *create_sensor_row(lv_obj_t *parent, ui_string_id_t title_id, lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *title = lv_label_create(row);
    register_localized_label(title, title_id);
    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, "--");
    if (value_label) {
        *value_label = value;
    }
    return value;
}

static lv_obj_t *create_slider_card(lv_obj_t *parent, ui_string_id_t title_id, slider_type_t type, slider_binding_t *binding)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(45), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_25, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    register_localized_label(lv_label_create(card), title_id);
    lv_obj_t *slider = lv_slider_create(card);
    lv_slider_set_range(slider, 0, 10000);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, binding);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_RELEASED, binding);

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, "0");

    binding->type = type;
    binding->slider = slider;
    binding->value_label = value_label;
    return card;
}

static lv_obj_t *create_spinbox(lv_obj_t *parent, ui_string_id_t title_id, int32_t min, int32_t max, uint32_t step, lv_obj_t **out)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 4, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 4, 0);
    register_localized_label(lv_label_create(container), title_id);
    lv_obj_t *spin = lv_spinbox_create(container);
    lv_spinbox_set_range(spin, min, max);
    lv_spinbox_set_step(spin, step);
    lv_obj_set_width(spin, 140);
    if (out) {
        *out = spin;
    }
    return spin;
}

static lv_obj_t *create_switch_row(lv_obj_t *parent, ui_string_id_t title_id, lv_obj_t **out_switch)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    register_localized_label(lv_label_create(row), title_id);
    lv_obj_t *sw = lv_switch_create(row);
    if (out_switch) {
        *out_switch = sw;
    }
    return sw;
}

static void register_localized_label(lv_obj_t *label, ui_string_id_t id)
{
    if (s_ctx.localized_label_count < LV_ARRAY_SIZE(s_ctx.localized_labels)) {
        s_ctx.localized_labels[s_ctx.localized_label_count++] = (localized_label_t){.label = label, .id = id};
    }
}

static void update_tab_titles(void)
{
    lv_tabview_set_tab_name(s_ctx.tabview, s_ctx.tab_dashboard_idx, ui_loc_get(s_ctx.language, UI_STR_TAB_DASHBOARD));
    lv_tabview_set_tab_name(s_ctx.tabview, s_ctx.tab_control_idx, ui_loc_get(s_ctx.language, UI_STR_TAB_CONTROL));
    lv_tabview_set_tab_name(s_ctx.tabview, s_ctx.tab_settings_idx, ui_loc_get(s_ctx.language, UI_STR_TAB_SETTINGS));
}

static void update_all_localized_labels(void)
{
    for (size_t i = 0; i < s_ctx.localized_label_count; ++i) {
        const localized_label_t *entry = &s_ctx.localized_labels[i];
        if (entry->label) {
            lv_label_set_text(entry->label, ui_loc_get(s_ctx.language, entry->id));
        }
    }
    update_tab_titles();
}

static void slider_event_cb(lv_event_t *e)
{
    slider_binding_t *binding = (slider_binding_t *)lv_event_get_user_data(e);
    if (!binding) {
        return;
    }
    int32_t value = lv_slider_get_value(binding->slider);
    update_slider_label(binding, value);
    if (s_ctx.updating_controls) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        send_light_command();
    }
}

static void spinbox_event_cb(lv_event_t *e)
{
    (void)e;
    if (!s_ctx.updating_controls) {
        send_light_command();
    }
}

static void mute_btn_event_cb(lv_event_t *e)
{
    (void)e;
    bool target = !s_ctx.alarm_muted;
    esp_err_t err = network_manager_set_alarm_mute(target);
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_ALARM, err);
    } else {
        s_ctx.alarm_muted = target;
        update_alarm_button();
    }
}

static void sky_dropdown_event_cb(lv_event_t *e)
{
    (void)e;
    send_light_command();
}

static void language_dropdown_event_cb(lv_event_t *e)
{
    (void)e;
    uint16_t idx = lv_dropdown_get_selected(s_ctx.dropdown_language);
    s_ctx.language = ui_loc_language_from_index(idx);
    strlcpy(s_ctx.config->language, ui_loc_to_code(s_ctx.language), sizeof(s_ctx.config->language));
    app_config_save(s_ctx.config);
    update_all_localized_labels();
    set_status_banner(ui_loc_get(s_ctx.language, UI_STR_STATUS_LANGUAGE_CHANGED), false);
}

static void species_apply_event_cb(lv_event_t *e)
{
    (void)e;
    uint16_t idx = lv_dropdown_get_selected(s_ctx.dropdown_species);
    if (idx >= s_ctx.species_option_count) {
        set_status_banner(ui_loc_get(s_ctx.language, UI_STR_SPECIES_NO_SELECTION), true);
        return;
    }
    const terrarium_species_entry_t *entry = s_ctx.species_option_entries[idx];
    if (!entry) {
        set_status_banner(ui_loc_get(s_ctx.language, UI_STR_SPECIES_NO_SELECTION), true);
        return;
    }
    esp_err_t err = network_manager_apply_species(entry->key);
    if (err == ESP_OK) {
        strlcpy(s_ctx.config->species_key, entry->key, sizeof(s_ctx.config->species_key));
        app_config_save(s_ctx.config);
        set_status_banner(ui_loc_get(s_ctx.language, UI_STR_SPECIES_APPLIED), false);
    } else {
        notify_error(UI_STR_ERROR_SPECIES, err);
    }
}

static void species_refresh_event_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = network_manager_request_species_catalog();
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_SPECIES, err);
    }
}

static void ota_upload_event_cb(lv_event_t *e)
{
    intptr_t role = (intptr_t)lv_event_get_user_data(e);
    const char *path = (role == OTA_ROLE_CONTROLLER) ? lv_textarea_get_text(s_ctx.txt_controller_path)
                                                      : lv_textarea_get_text(s_ctx.txt_dome_path);
    if (!path || path[0] == '\0') {
        set_status_banner(ui_loc_get(s_ctx.language, UI_STR_OTA_NO_PATH), true);
        return;
    }
    esp_err_t err = (role == OTA_ROLE_CONTROLLER) ? network_manager_upload_controller_ota(path)
                                                   : network_manager_upload_dome_ota(path);
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_OTA, err);
    } else {
        set_status_banner(ui_loc_get(s_ctx.language, UI_STR_OTA_IN_PROGRESS), false);
    }
}

static void calibration_fetch_event_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = network_manager_fetch_calibration();
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_CALIBRATION, err);
    }
}

static void calibration_apply_event_cb(lv_event_t *e)
{
    (void)e;
    terrarium_uvb_calibration_command_t cmd = {
        .k = lv_spinbox_get_value(s_ctx.spin_calib_k) / 100.0f,
        .uvi_max = lv_spinbox_get_value(s_ctx.spin_calib_uvi) / 100.0f,
    };
    esp_err_t err = network_manager_post_calibration(&cmd);
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_CALIBRATION, err);
    }
}

static void settings_save_event_cb(lv_event_t *e)
{
    (void)e;
    strlcpy(s_ctx.config->ssid, lv_textarea_get_text(s_ctx.ta_ssid), sizeof(s_ctx.config->ssid));
    strlcpy(s_ctx.config->password, lv_textarea_get_text(s_ctx.ta_password), sizeof(s_ctx.config->password));
    strlcpy(s_ctx.config->controller_host, lv_textarea_get_text(s_ctx.ta_host), sizeof(s_ctx.config->controller_host));
    s_ctx.config->controller_port = lv_spinbox_get_value(s_ctx.spin_port);
    s_ctx.config->use_tls = lv_obj_has_state(s_ctx.sw_tls, LV_STATE_CHECKED);
    esp_err_t err = app_config_save(s_ctx.config);
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_CONFIG, err);
        return;
    }
    err = network_manager_init(s_ctx.config);
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_NETWORK, err);
    } else {
        set_status_banner(ui_loc_get(s_ctx.language, UI_STR_NETWORK_SAVED), false);
    }
}

static void network_status_cb(const terrarium_status_t *status, void *ctx)
{
    (void)ctx;
    if (!status) {
        return;
    }
    s_ctx.last_status = *status;
    lv_async_call(ui_apply_status_async, NULL);
}

static void network_error_cb(esp_err_t err, const char *message, void *ctx)
{
    (void)ctx;
    char buf[160];
    const char *prefix = ui_loc_get(s_ctx.language, UI_STR_ERROR_NETWORK);
    if (message && message[0]) {
        snprintf(buf, sizeof(buf), "%s: %s (%s)", prefix, message, esp_err_to_name(err));
    } else {
        snprintf(buf, sizeof(buf), "%s (%s)", prefix, esp_err_to_name(err));
    }
    set_status_banner(buf, true);
}

static void network_species_cb(const terrarium_species_catalog_t *catalog, void *ctx)
{
    (void)ctx;
    if (!catalog) {
        return;
    }
    s_ctx.species_catalog = *catalog;
    lv_async_call(ui_apply_species_async, NULL);
}

static void ui_apply_species_async(void *param)
{
    (void)param;
    lvgl_port_lock();
    apply_species_to_dropdown();
    lvgl_port_unlock();
}

static void ui_apply_status_async(void *param)
{
    (void)param;
    lvgl_port_lock();
    apply_status_to_widgets(&s_ctx.last_status);
    lvgl_port_unlock();
}

static void apply_status_to_widgets(const terrarium_status_t *status)
{
    if (!status || !status->valid) {
        return;
    }
    s_ctx.updating_controls = true;

    if (status->summary[0]) {
        lv_label_set_text(s_ctx.label_summary, status->summary);
    }

    const char *unknown = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_UNKNOWN);
    if (s_ctx.label_sensor_sht31) {
        lv_label_set_text(s_ctx.label_sensor_sht31, unknown);
    }
    if (s_ctx.label_sensor_sht21) {
        lv_label_set_text(s_ctx.label_sensor_sht21, unknown);
    }
    if (s_ctx.label_sensor_bme280) {
        lv_label_set_text(s_ctx.label_sensor_bme280, unknown);
    }
    if (s_ctx.label_sensor_ds18b20) {
        lv_label_set_text(s_ctx.label_sensor_ds18b20, unknown);
    }
    if (s_ctx.label_sensor_ambient) {
        lv_label_set_text(s_ctx.label_sensor_ambient, unknown);
    }

    if (status->env.valid) {
        const char *fmt_temp_hum = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_TEMP_HUM);
        const char *fmt_hum = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_HUM);
        const char *fmt_press = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_PRESSURE_HUM);
        const char *fmt_uvi = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_UVI);
        if (s_ctx.label_sensor_sht31) {
            lv_label_set_text_fmt(s_ctx.label_sensor_sht31, fmt_temp_hum, status->env.temperature_c,
                                  status->env.humidity_percent);
        }
        if (s_ctx.label_sensor_sht21) {
            lv_label_set_text_fmt(s_ctx.label_sensor_sht21, fmt_hum, status->env.humidity_percent);
        }
        if (s_ctx.label_sensor_bme280) {
            lv_label_set_text_fmt(s_ctx.label_sensor_bme280, fmt_press, status->env.pressure_hpa,
                                  status->env.humidity_percent);
        }
    }

    if (status->dome.valid) {
        const char *state_txt = status->dome.status ? ui_loc_get(s_ctx.language, UI_STR_DOME_ACTIVE)
                                                   : ui_loc_get(s_ctx.language, UI_STR_DOME_IDLE);
        lv_label_set_text(s_ctx.label_dome_state, state_txt);
        if (status->dome.flags & 0x01) {
            lv_led_on(s_ctx.led_interlock);
            lv_label_set_text(s_ctx.label_interlock, ui_loc_get(s_ctx.language, UI_STR_INTERLOCK_ACTIVE));
        } else {
            lv_led_off(s_ctx.led_interlock);
            lv_label_set_text(s_ctx.label_interlock, ui_loc_get(s_ctx.language, UI_STR_INTERLOCK_OK));
        }
        if (s_ctx.label_sensor_ds18b20) {
            const char *fmt = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_HEATSINK);
            lv_label_set_text_fmt(s_ctx.label_sensor_ds18b20, fmt, status->dome.heatsink_c);
        }
    }

    if (s_ctx.label_sensor_ambient) {
        if (status->dome.valid && status->dome.uvi_fault) {
            lv_label_set_text(s_ctx.label_sensor_ambient,
                              ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_UVI_FAULT));
        } else if (status->climate.valid && status->climate.uvi_valid) {
            const char *fmt = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_UVI_EXT);
            lv_label_set_text_fmt(s_ctx.label_sensor_ambient, fmt,
                                  status->climate.uvi_measured,
                                  status->climate.uvi_error,
                                  status->climate.irradiance_uW_cm2);
        } else {
            float fallback_uvi = status->env.valid ? status->env.uvi : status->dome.uvi;
            const char *fmt = ui_loc_get(s_ctx.language, UI_STR_SENSOR_VALUE_UVI);
            lv_label_set_text_fmt(s_ctx.label_sensor_ambient, fmt, fallback_uvi);
        }
    }

    if (status->light.valid) {
        lv_slider_set_value(s_ctx.sliders[SLIDER_CCT_DAY].slider, status->light.cct_day, LV_ANIM_OFF);
        lv_slider_set_value(s_ctx.sliders[SLIDER_CCT_WARM].slider, status->light.cct_warm, LV_ANIM_OFF);
        lv_slider_set_value(s_ctx.sliders[SLIDER_UVA_SET].slider, status->light.uva_set, LV_ANIM_OFF);
        lv_slider_set_value(s_ctx.sliders[SLIDER_UVA_CLAMP].slider, status->light.uva_clamp, LV_ANIM_OFF);
        lv_slider_set_value(s_ctx.sliders[SLIDER_UVB_SET].slider, status->light.uvb_set, LV_ANIM_OFF);
        lv_slider_set_value(s_ctx.sliders[SLIDER_UVB_CLAMP].slider, status->light.uvb_clamp, LV_ANIM_OFF);
        lv_spinbox_set_value(s_ctx.spin_uvb_period, status->light.uvb_period_s);
        lv_spinbox_set_value(s_ctx.spin_uvb_duty, status->light.uvb_duty_pm);
        lv_dropdown_set_selected(s_ctx.dropdown_sky, status->light.sky_mode);
        for (size_t i = 0; i < SLIDER_COUNT; ++i) {
            update_slider_label(&s_ctx.sliders[i], lv_slider_get_value(s_ctx.sliders[i].slider));
        }
    }

    if (status->uvb_calibration.valid) {
        const char *fmt = ui_loc_get(s_ctx.language, UI_STR_CALIB_STATUS);
        lv_label_set_text_fmt(s_ctx.label_calib_status, fmt, status->uvb_calibration.k, status->uvb_calibration.uvi_max);
    } else if (s_ctx.label_calib_status) {
        lv_label_set_text(s_ctx.label_calib_status, "");
    }

    s_ctx.alarm_muted = status->alarm_muted;
    update_alarm_button();

    if (status->env.valid) {
        float uvi = status->climate.valid && status->climate.uvi_valid
                        ? status->climate.uvi_measured
                        : (status->dome.valid ? status->dome.uvi : status->env.uvi);
        store_history_sample(status->env.temperature_c, status->env.humidity_percent, uvi);
    }
    refresh_chart();

    char banner[128];
    snprintf(banner, sizeof(banner), ui_loc_get(s_ctx.language, UI_STR_STATUS_LAST_UPDATE),
             (unsigned long long)status->timestamp_ms);
    set_status_banner(banner, false);

    s_ctx.updating_controls = false;
}

static void update_slider_label(slider_binding_t *binding, int32_t value)
{
    if (!binding || !binding->value_label) {
        return;
    }
    lv_label_set_text_fmt(binding->value_label, "%d", value);
}

static void set_status_banner_locked(const char *text, bool error)
{
    lv_label_set_text(s_ctx.label_status_banner, text);
    lv_obj_set_style_text_color(s_ctx.label_status_banner,
                                error ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN), 0);
}

static void set_status_banner(const char *text, bool error)
{
    status_banner_msg_t *msg = lv_malloc(sizeof(status_banner_msg_t));
    if (!msg) {
        return;
    }
    msg->error = error;
    strlcpy(msg->text, text, sizeof(msg->text));
    lv_async_call(status_banner_async, msg);
}

static void status_banner_async(void *param)
{
    status_banner_msg_t *msg = param;
    lvgl_port_lock();
    set_status_banner_locked(msg->text, msg->error);
    lvgl_port_unlock();
    lv_free(msg);
}

static void notify_error(ui_string_id_t prefix_id, esp_err_t err)
{
    char buf[160];
    const char *prefix = ui_loc_get(s_ctx.language, prefix_id);
    snprintf(buf, sizeof(buf), "%s (%s)", prefix, esp_err_to_name(err));
    set_status_banner(buf, true);
}

static void send_light_command(void)
{
    terrarium_light_command_t cmd = {
        .cct_day = lv_slider_get_value(s_ctx.sliders[SLIDER_CCT_DAY].slider),
        .cct_warm = lv_slider_get_value(s_ctx.sliders[SLIDER_CCT_WARM].slider),
        .uva_set = lv_slider_get_value(s_ctx.sliders[SLIDER_UVA_SET].slider),
        .uva_clamp = lv_slider_get_value(s_ctx.sliders[SLIDER_UVA_CLAMP].slider),
        .uvb_set = lv_slider_get_value(s_ctx.sliders[SLIDER_UVB_SET].slider),
        .uvb_clamp = lv_slider_get_value(s_ctx.sliders[SLIDER_UVB_CLAMP].slider),
        .uvb_period_s = lv_spinbox_get_value(s_ctx.spin_uvb_period),
        .uvb_duty_pm = lv_spinbox_get_value(s_ctx.spin_uvb_duty),
        .sky = lv_dropdown_get_selected(s_ctx.dropdown_sky),
    };
    esp_err_t err = network_manager_post_light(&cmd);
    if (err != ESP_OK) {
        notify_error(UI_STR_ERROR_LIGHT, err);
    }
}

static void update_alarm_button(void)
{
    if (s_ctx.alarm_muted) {
        lv_label_set_text(s_ctx.btn_mute_label, ui_loc_get(s_ctx.language, UI_STR_ALARM_UNMUTE));
        lv_obj_add_state(s_ctx.btn_mute, LV_STATE_CHECKED);
    } else {
        lv_label_set_text(s_ctx.btn_mute_label, ui_loc_get(s_ctx.language, UI_STR_ALARM_MUTE));
        lv_obj_clear_state(s_ctx.btn_mute, LV_STATE_CHECKED);
    }
}

static void populate_settings_form(void)
{
    lv_textarea_set_text(s_ctx.ta_ssid, s_ctx.config->ssid);
    lv_textarea_set_text(s_ctx.ta_password, s_ctx.config->password);
    lv_textarea_set_text(s_ctx.ta_host, s_ctx.config->controller_host);
    lv_spinbox_set_value(s_ctx.spin_port, s_ctx.config->controller_port);
    if (s_ctx.config->use_tls) {
        lv_obj_add_state(s_ctx.sw_tls, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_ctx.sw_tls, LV_STATE_CHECKED);
    }
    lv_dropdown_set_selected(s_ctx.dropdown_language, ui_loc_language_index(s_ctx.language));
}

static void store_history_sample(float temp, float hum, float uvi)
{
    if (!s_ctx.history_temp || !s_ctx.history_hum || !s_ctx.history_uvi) {
        return;
    }
    size_t idx = s_ctx.history_head;
    s_ctx.history_temp[idx] = temp;
    s_ctx.history_hum[idx] = hum;
    s_ctx.history_uvi[idx] = uvi;
    s_ctx.history_head = (idx + 1) % s_ctx.history_capacity;
    if (s_ctx.history_count < s_ctx.history_capacity) {
        s_ctx.history_count++;
    }
}

static void refresh_chart(void)
{
    if (!s_ctx.chart) {
        return;
    }
    size_t visible = CHART_VISIBLE_POINTS;
    if (visible > s_ctx.history_count) {
        visible = s_ctx.history_count;
    }
    size_t start = (s_ctx.history_head + s_ctx.history_capacity - visible) % s_ctx.history_capacity;
    for (size_t i = 0; i < CHART_VISIBLE_POINTS; ++i) {
        s_ctx.chart_temp->y_points[i] = 0;
        s_ctx.chart_hum->y_points[i] = 0;
        s_ctx.chart_uvi->y_points[i] = 0;
    }
    for (size_t i = 0; i < visible; ++i) {
        size_t idx = (start + i) % s_ctx.history_capacity;
        size_t dest = (CHART_VISIBLE_POINTS - visible) + i;
        s_ctx.chart_temp->y_points[dest] = s_ctx.history_temp[idx];
        s_ctx.chart_hum->y_points[dest] = s_ctx.history_hum[idx];
        s_ctx.chart_uvi->y_points[dest] = s_ctx.history_uvi[idx] * 10.0f;
    }
    lv_chart_refresh(s_ctx.chart);
}

static void apply_species_to_dropdown(void)
{
    if (!s_ctx.dropdown_species) {
        return;
    }
    s_ctx.species_option_count = 0;
    memset(s_ctx.species_option_entries, 0, sizeof(s_ctx.species_option_entries));
    if (s_ctx.species_catalog.count == 0) {
        lv_dropdown_set_options(s_ctx.dropdown_species, "");
        return;
    }
    char options[NETWORK_SPECIES_MAX_ENTRIES * NETWORK_MAX_SPECIES_LABEL];
    options[0] = '\0';
    for (size_t i = 0; i < s_ctx.species_catalog.count; ++i) {
        const terrarium_species_entry_t *entry = &s_ctx.species_catalog.entries[i];
        const char *label = ui_loc_select_label(entry, s_ctx.language);
        if (!label) {
            label = entry->key;
        }
        if (options[0]) {
            strlcat(options, "\n", sizeof(options));
        }
        strlcat(options, label, sizeof(options));
        if (s_ctx.species_option_count < LV_ARRAY_SIZE(s_ctx.species_option_entries)) {
            s_ctx.species_option_entries[s_ctx.species_option_count++] = entry;
        }
    }
    lv_dropdown_set_options(s_ctx.dropdown_species, options);
    const char *active_key = s_ctx.config->species_key[0] ? s_ctx.config->species_key : s_ctx.species_catalog.active_key;
    if (active_key[0]) {
        for (size_t i = 0; i < s_ctx.species_option_count; ++i) {
            const terrarium_species_entry_t *entry = s_ctx.species_option_entries[i];
            if (entry && strcmp(entry->key, active_key) == 0) {
                lv_dropdown_set_selected(s_ctx.dropdown_species, i);
                break;
            }
        }
    }
}

