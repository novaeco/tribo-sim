#include "ui_main.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "display_driver.h"
#include "network_manager.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "ui"

typedef enum {
    SLIDER_CCT_DAY,
    SLIDER_CCT_WARM,
    SLIDER_UVA,
    SLIDER_UVB,
} slider_type_t;

typedef struct {
    slider_type_t type;
    lv_obj_t *slider;
    lv_obj_t *value_label;
} slider_binding_t;

typedef struct {
    app_config_t *config;
    slider_binding_t sliders[4];
    lv_obj_t *spin_uvb_period;
    lv_obj_t *spin_uvb_duty;
    lv_obj_t *sky_button;
    lv_obj_t *sky_label;
    lv_obj_t *label_status_banner;
    lv_obj_t *label_sensor_sht31;
    lv_obj_t *label_sensor_sht21;
    lv_obj_t *label_sensor_bme280;
    lv_obj_t *label_sensor_ds18b20;
    lv_obj_t *label_sensor_ambient;
    lv_obj_t *label_dome_state;
    lv_obj_t *led_interlock;
    lv_obj_t *label_interlock;
    lv_obj_t *btn_mute;
    lv_obj_t *btn_mute_label;
    lv_obj_t *spin_calib_k;
    lv_obj_t *spin_calib_uvi;
    lv_obj_t *label_calib_status;
    lv_obj_t *ta_ssid;
    lv_obj_t *ta_password;
    lv_obj_t *ta_host;
    lv_obj_t *spin_port;
    lv_obj_t *sw_tls;
    bool alarm_muted;
    bool updating_controls;
    terrarium_status_t last_status;
} ui_context_t;

typedef struct {
    bool error;
    char text[128];
} status_banner_msg_t;

static ui_context_t s_ctx;

static void slider_event_cb(lv_event_t *e);
static void mute_btn_event_cb(lv_event_t *e);
static void sky_btn_event_cb(lv_event_t *e);
static void spinbox_event_cb(lv_event_t *e);
static void calibration_fetch_event_cb(lv_event_t *e);
static void calibration_apply_event_cb(lv_event_t *e);
static void settings_save_event_cb(lv_event_t *e);
static void network_status_cb(const terrarium_status_t *status, void *ctx);
static void network_error_cb(esp_err_t err, const char *message, void *ctx);
static void ui_apply_status_async(void *param);
static void update_slider_label(slider_binding_t *binding, int32_t value);
static void update_alarm_button(void);
static void apply_status_to_widgets(const terrarium_status_t *status);
static void populate_settings_form(void);
static void send_light_command(void);
static void status_banner_async(void *param);
static void set_status_banner(const char *text, bool error);
static void set_status_banner_locked(const char *text, bool error);
static void notify_errorf(const char *prefix, esp_err_t err);

static lv_obj_t *create_sensor_row(lv_obj_t *parent, const char *title, lv_obj_t **value_label);
static lv_obj_t *create_slider_card(lv_obj_t *parent, const char *title, slider_type_t type, slider_binding_t *binding);
static lv_obj_t *create_spinbox(lv_obj_t *parent, const char *title, int32_t min, int32_t max, uint32_t step, lv_obj_t **out_spin);
static lv_obj_t *create_labeled_switch(lv_obj_t *parent, const char *title, lv_obj_t **out_switch);

esp_err_t ui_init(app_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.config = config;

    lvgl_port_lock();

    lv_disp_t *disp = lvgl_port_get_display();
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *tabview = lv_tabview_create(scr, LV_DIR_TOP, 50);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));

    lv_obj_t *tab_control = lv_tabview_add_tab(tabview, "Contrôle");
    lv_obj_t *tab_calib = lv_tabview_add_tab(tabview, "Calibration UVI");

    // Control tab layout
    lv_obj_set_flex_flow(tab_control, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_control, 12, 0);

    lv_obj_t *status_banner = lv_label_create(tab_control);
    lv_label_set_text(status_banner, "Connexion en cours...");
    lv_obj_set_style_text_color(status_banner, lv_palette_main(LV_PALETTE_BLUE), 0);
    s_ctx.label_status_banner = status_banner;

    lv_obj_t *sensor_card = lv_obj_create(tab_control);
    lv_obj_set_size(sensor_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(sensor_card, 12, 0);
    lv_obj_set_style_radius(sensor_card, 12, 0);
    lv_obj_set_flex_flow(sensor_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(sensor_card, LV_OPA_20, 0);

    create_sensor_row(sensor_card, "SHT31", &s_ctx.label_sensor_sht31);
    create_sensor_row(sensor_card, "SHT21", &s_ctx.label_sensor_sht21);
    create_sensor_row(sensor_card, "BME280", &s_ctx.label_sensor_bme280);
    create_sensor_row(sensor_card, "DS18B20", &s_ctx.label_sensor_ds18b20);
    create_sensor_row(sensor_card, "Ambiant", &s_ctx.label_sensor_ambient);

    lv_obj_t *dome_state = lv_obj_create(tab_control);
    lv_obj_set_size(dome_state, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(dome_state, 12, 0);
    lv_obj_set_style_radius(dome_state, 12, 0);
    lv_obj_set_style_bg_opa(dome_state, LV_OPA_20, 0);
    lv_obj_set_flex_flow(dome_state, LV_FLEX_FLOW_COLUMN);
    s_ctx.label_dome_state = lv_label_create(dome_state);
    lv_label_set_text(s_ctx.label_dome_state, "Dôme: --");

    lv_obj_t *interlock_row = lv_obj_create(dome_state);
    lv_obj_set_size(interlock_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(interlock_row, 4, 0);
    lv_obj_set_style_bg_opa(interlock_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(interlock_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(interlock_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_ctx.led_interlock = lv_led_create(interlock_row);
    lv_led_set_color(s_ctx.led_interlock, lv_palette_main(LV_PALETTE_RED));
    lv_led_off(s_ctx.led_interlock);
    s_ctx.label_interlock = lv_label_create(interlock_row);
    lv_label_set_text(s_ctx.label_interlock, "Interlock: --");

    s_ctx.btn_mute = lv_btn_create(dome_state);
    lv_obj_add_event_cb(s_ctx.btn_mute, mute_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_ctx.btn_mute_label = lv_label_create(s_ctx.btn_mute);
    lv_label_set_text(s_ctx.btn_mute_label, "Mute alarmes");

    lv_obj_t *controls_row = lv_obj_create(tab_control);
    lv_obj_set_size(controls_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(controls_row, 12, 0);
    lv_obj_set_style_radius(controls_row, 12, 0);
    lv_obj_set_style_bg_opa(controls_row, LV_OPA_20, 0);
    lv_obj_set_flex_flow(controls_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(controls_row, 12, 0);
    lv_obj_set_style_pad_column(controls_row, 12, 0);

    create_slider_card(controls_row, "CCT Day", SLIDER_CCT_DAY, &s_ctx.sliders[SLIDER_CCT_DAY]);
    create_slider_card(controls_row, "CCT Warm", SLIDER_CCT_WARM, &s_ctx.sliders[SLIDER_CCT_WARM]);
    create_slider_card(controls_row, "UVA", SLIDER_UVA, &s_ctx.sliders[SLIDER_UVA]);
    create_slider_card(controls_row, "UVB", SLIDER_UVB, &s_ctx.sliders[SLIDER_UVB]);

    lv_obj_t *uvb_config_card = lv_obj_create(tab_control);
    lv_obj_set_size(uvb_config_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(uvb_config_card, 12, 0);
    lv_obj_set_style_radius(uvb_config_card, 12, 0);
    lv_obj_set_style_bg_opa(uvb_config_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(uvb_config_card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(uvb_config_card, 12, 0);
    lv_obj_set_style_pad_column(uvb_config_card, 12, 0);

    create_spinbox(uvb_config_card, "UVB période (s)", 1, 3600, 1, &s_ctx.spin_uvb_period);
    lv_obj_add_event_cb(s_ctx.spin_uvb_period, spinbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_spinbox(uvb_config_card, "UVB duty (‰)", 0, 1000, 5, &s_ctx.spin_uvb_duty);
    lv_obj_add_event_cb(s_ctx.spin_uvb_duty, spinbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *sky_card = lv_obj_create(tab_control);
    lv_obj_set_size(sky_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(sky_card, 12, 0);
    lv_obj_set_style_radius(sky_card, 12, 0);
    lv_obj_set_style_bg_opa(sky_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(sky_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sky_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_ctx.sky_button = lv_btn_create(sky_card);
    lv_obj_add_event_cb(s_ctx.sky_button, sky_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_ctx.sky_label = lv_label_create(s_ctx.sky_button);
    lv_label_set_text(s_ctx.sky_label, "Sky: Off");

    // Calibration tab content
    lv_obj_set_flex_flow(tab_calib, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_calib, 12, 0);

    lv_obj_t *calib_card = lv_obj_create(tab_calib);
    lv_obj_set_size(calib_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(calib_card, 12, 0);
    lv_obj_set_style_radius(calib_card, 12, 0);
    lv_obj_set_style_bg_opa(calib_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(calib_card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(calib_card, 12, 0);
    lv_obj_set_style_pad_column(calib_card, 12, 0);

    create_spinbox(calib_card, "UVB k", 0, 10000, 1, &s_ctx.spin_calib_k);
    lv_spinbox_set_digit_format(s_ctx.spin_calib_k, 6, 2);
    create_spinbox(calib_card, "UVI max", 0, 1000, 1, &s_ctx.spin_calib_uvi);
    lv_spinbox_set_digit_format(s_ctx.spin_calib_uvi, 6, 2);

    lv_obj_t *calib_btn_row = lv_obj_create(tab_calib);
    lv_obj_set_style_bg_opa(calib_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(calib_btn_row, 0, 0);
    lv_obj_set_flex_flow(calib_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(calib_btn_row, 12, 0);

    lv_obj_t *btn_fetch = lv_btn_create(calib_btn_row);
    lv_obj_add_event_cb(btn_fetch, calibration_fetch_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_fetch), "Lire calibration");

    lv_obj_t *btn_apply = lv_btn_create(calib_btn_row);
    lv_obj_add_event_cb(btn_apply, calibration_apply_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_apply), "Appliquer calibration");

    s_ctx.label_calib_status = lv_label_create(tab_calib);
    lv_label_set_text(s_ctx.label_calib_status, "Calibration: inconnue");

    lv_obj_t *settings_card = lv_obj_create(tab_calib);
    lv_obj_set_size(settings_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(settings_card, 12, 0);
    lv_obj_set_style_radius(settings_card, 12, 0);
    lv_obj_set_style_bg_opa(settings_card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(settings_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_card, 8, 0);

    lv_label_set_text(lv_label_create(settings_card), "Paramètres réseau");

    lv_obj_t *ssid_row = lv_textarea_create(settings_card);
    lv_textarea_set_one_line(ssid_row, true);
    lv_textarea_set_max_length(ssid_row, APP_CONFIG_MAX_SSID_LEN);
    lv_obj_set_width(ssid_row, LV_PCT(100));
    s_ctx.ta_ssid = ssid_row;

    lv_obj_t *pass_row = lv_textarea_create(settings_card);
    lv_textarea_set_one_line(pass_row, true);
    lv_textarea_set_password_mode(pass_row, true);
    lv_textarea_set_max_length(pass_row, APP_CONFIG_MAX_PASSWORD_LEN);
    lv_obj_set_width(pass_row, LV_PCT(100));
    s_ctx.ta_password = pass_row;

    lv_obj_t *host_row = lv_textarea_create(settings_card);
    lv_textarea_set_one_line(host_row, true);
    lv_textarea_set_max_length(host_row, APP_CONFIG_MAX_HOST_LEN);
    lv_obj_set_width(host_row, LV_PCT(100));
    s_ctx.ta_host = host_row;

    create_spinbox(settings_card, "Port contrôleur", 1, 65535, 1, &s_ctx.spin_port);
    create_labeled_switch(settings_card, "Utiliser HTTPS", &s_ctx.sw_tls);

    lv_obj_t *btn_save = lv_btn_create(settings_card);
    lv_obj_add_event_cb(btn_save, settings_save_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_save), "Sauvegarder paramètres");

    populate_settings_form();

    lvgl_port_unlock();

    network_manager_register_status_callback(network_status_cb, NULL);
    network_manager_register_error_callback(network_error_cb, NULL);

    return ESP_OK;
}

void ui_show_error(esp_err_t err, const char *message)
{
    lvgl_port_lock();
    char buf[128];
    snprintf(buf, sizeof(buf), "%s (%s)", message ? message : "Erreur", esp_err_to_name(err));
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Erreur", buf, NULL, true);
    lv_obj_center(mbox);
    lvgl_port_unlock();
}

static lv_obj_t *create_sensor_row(lv_obj_t *parent, const char *title, lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_label_set_text(lv_label_create(row), title);
    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, "--");
    if (value_label) {
        *value_label = value;
    }
    return row;
}

static lv_obj_t *create_slider_card(lv_obj_t *parent, const char *title, slider_type_t type, slider_binding_t *binding)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_30, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    lv_label_set_text(lv_label_create(card), title);

    lv_obj_t *slider = lv_slider_create(card);
    lv_slider_set_range(slider, 0, 10000);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_RELEASED, binding);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, binding);

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, "0");

    binding->type = type;
    binding->slider = slider;
    binding->value_label = value_label;

    return card;
}

static lv_obj_t *create_spinbox(lv_obj_t *parent, const char *title, int32_t min, int32_t max, uint32_t step, lv_obj_t **out_spin)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);

    lv_label_set_text(lv_label_create(cont), title);

    lv_obj_t *spin = lv_spinbox_create(cont);
    lv_spinbox_set_range(spin, min, max);
    lv_spinbox_set_digit_format(spin, 5, 0);
    lv_spinbox_set_step(spin, step);
    lv_obj_set_width(spin, 120);

    if (out_spin) {
        *out_spin = spin;
    }
    return cont;
}

static lv_obj_t *create_labeled_switch(lv_obj_t *parent, const char *title, lv_obj_t **out_switch)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_label_set_text(lv_label_create(row), title);
    lv_obj_t *sw = lv_switch_create(row);
    if (out_switch) {
        *out_switch = sw;
    }
    return row;
}

static void slider_event_cb(lv_event_t *e)
{
    slider_binding_t *binding = (slider_binding_t *)lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    if (!binding || !slider) {
        return;
    }
    int32_t value = lv_slider_get_value(slider);
    update_slider_label(binding, value);
    if (s_ctx.updating_controls) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_VALUE_CHANGED) {
        send_light_command();
    }
}

static void spinbox_event_cb(lv_event_t *e)
{
    if (s_ctx.updating_controls) {
        return;
    }
    send_light_command();
}

static void mute_btn_event_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = network_manager_set_alarm_mute(!s_ctx.alarm_muted);
    notify_errorf("Mute alarmes", err);
}

static void sky_btn_event_cb(lv_event_t *e)
{
    (void)e;
    uint8_t sky = s_ctx.last_status.dome.sky_mode;
    sky = (sky + 1) % 3;
    s_ctx.last_status.dome.sky_mode = sky;
    const char *sky_text = "Sky: Off";
    switch (sky) {
    case 1: sky_text = "Sky: Bleu"; break;
    case 2: sky_text = "Sky: Twinkle"; break;
    default: break;
    }
    lv_label_set_text(s_ctx.sky_label, sky_text);
    send_light_command();
}

static void calibration_fetch_event_cb(lv_event_t *e)
{
    (void)e;
    lv_label_set_text(s_ctx.label_calib_status, "Lecture en cours...");
    set_status_banner("Lecture des coefficients...", false);
    esp_err_t err = network_manager_fetch_calibration();
    if (err != ESP_OK) {
        lv_label_set_text(s_ctx.label_calib_status, "Échec lecture calibration");
    }
    notify_errorf("Lecture calibration", err);
}

static void calibration_apply_event_cb(lv_event_t *e)
{
    (void)e;
    terrarium_uvb_calibration_command_t cmd = {
        .k = (float)lv_spinbox_get_value(s_ctx.spin_calib_k),
        .uvi_max = (float)lv_spinbox_get_value(s_ctx.spin_calib_uvi),
    };
    lv_label_set_text(s_ctx.label_calib_status, "Envoi de la calibration...");
    esp_err_t err = network_manager_post_calibration(&cmd);
    if (err == ESP_OK) {
        set_status_banner("Calibration UVB envoyée", false);
    } else {
        lv_label_set_text(s_ctx.label_calib_status, "Erreur d'envoi calibration");
    }
    notify_errorf("Calibration UVB", err);
}

static void settings_save_event_cb(lv_event_t *e)
{
    (void)e;
    strlcpy(s_ctx.config->ssid, lv_textarea_get_text(s_ctx.ta_ssid), sizeof(s_ctx.config->ssid));
    strlcpy(s_ctx.config->password, lv_textarea_get_text(s_ctx.ta_password), sizeof(s_ctx.config->password));
    strlcpy(s_ctx.config->controller_host, lv_textarea_get_text(s_ctx.ta_host), sizeof(s_ctx.config->controller_host));
    s_ctx.config->controller_port = (uint16_t)lv_spinbox_get_value(s_ctx.spin_port);
    s_ctx.config->use_tls = lv_obj_has_state(s_ctx.sw_tls, LV_STATE_CHECKED);

    if (app_config_save(s_ctx.config) == ESP_OK) {
        lv_label_set_text(s_ctx.label_calib_status, "Paramètres sauvegardés. Redémarrez pour appliquer.");
        set_status_banner("Paramètres sauvegardés", false);
    } else {
        lv_label_set_text(s_ctx.label_calib_status, "Échec de la sauvegarde des paramètres.");
        set_status_banner("Échec sauvegarde paramètres", true);
    }
}

static void update_slider_label(slider_binding_t *binding, int32_t value)
{
    if (!binding || !binding->value_label) {
        return;
    }
    lv_label_set_text_fmt(binding->value_label, "%d", (int)value);
}

static void network_status_cb(const terrarium_status_t *status, void *ctx)
{
    (void)ctx;
    if (!status) {
        return;
    }
    s_ctx.last_status = *status;
    lv_async_call(ui_apply_status_async, &s_ctx);
}

static void network_error_cb(esp_err_t err, const char *message, void *ctx)
{
    (void)ctx;
    notify_errorf(message ? message : "Erreur réseau", err);
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

    if (status->sensors.sht31.valid) {
        lv_label_set_text_fmt(s_ctx.label_sensor_sht31, "%.1f °C / %.1f %%", status->sensors.sht31.temperature_c, status->sensors.sht31.humidity_percent);
    }
    if (status->sensors.sht21.valid) {
        lv_label_set_text_fmt(s_ctx.label_sensor_sht21, "%.1f °C / %.1f %%", status->sensors.sht21.temperature_c, status->sensors.sht21.humidity_percent);
    }
    if (status->sensors.bme280.valid) {
        lv_label_set_text_fmt(s_ctx.label_sensor_bme280, "%.1f °C / %.1f %%", status->sensors.bme280.temperature_c, status->sensors.bme280.humidity_percent);
    }
    if (status->sensors.ds18b20.valid) {
        lv_label_set_text_fmt(s_ctx.label_sensor_ds18b20, "%.1f °C", status->sensors.ds18b20.temperature_c);
    }
    if (status->sensors.ambient.valid) {
        lv_label_set_text_fmt(s_ctx.label_sensor_ambient, "%.1f °C / %.1f %%", status->sensors.ambient.temperature_c, status->sensors.ambient.humidity_percent);
    }

    lv_label_set_text_fmt(s_ctx.label_dome_state, "Dôme: %s", status->dome.status ? "Actif" : "Arrêt");
    if (status->dome.interlock) {
        lv_led_on(s_ctx.led_interlock);
        lv_label_set_text(s_ctx.label_interlock, "Interlock: actif");
    } else {
        lv_led_off(s_ctx.led_interlock);
        lv_label_set_text(s_ctx.label_interlock, "Interlock: OK");
    }

    s_ctx.alarm_muted = status->dome.alarm_muted;
    update_alarm_button();

    char status_msg[128];
    snprintf(status_msg, sizeof(status_msg), "Dernière mise à jour: %llu ms", (unsigned long long)status->timestamp_ms);
    set_status_banner_locked(status_msg, false);

    lv_slider_set_value(s_ctx.sliders[SLIDER_CCT_DAY].slider, status->dome.cct_day, LV_ANIM_OFF);
    lv_slider_set_value(s_ctx.sliders[SLIDER_CCT_WARM].slider, status->dome.cct_warm, LV_ANIM_OFF);
    lv_slider_set_value(s_ctx.sliders[SLIDER_UVA].slider, status->dome.uva_set, LV_ANIM_OFF);
    lv_slider_set_value(s_ctx.sliders[SLIDER_UVB].slider, status->dome.uvb_set, LV_ANIM_OFF);

    update_slider_label(&s_ctx.sliders[SLIDER_CCT_DAY], status->dome.cct_day);
    update_slider_label(&s_ctx.sliders[SLIDER_CCT_WARM], status->dome.cct_warm);
    update_slider_label(&s_ctx.sliders[SLIDER_UVA], status->dome.uva_set);
    update_slider_label(&s_ctx.sliders[SLIDER_UVB], status->dome.uvb_set);

    lv_spinbox_set_value(s_ctx.spin_uvb_period, status->dome.uvb_period_s);
    lv_spinbox_set_value(s_ctx.spin_uvb_duty, status->dome.uvb_duty_pm);

    const char *sky_text = "Sky: Off";
    switch (status->dome.sky_mode) {
    case 1: sky_text = "Sky: Bleu"; break;
    case 2: sky_text = "Sky: Twinkle"; break;
    default: sky_text = "Sky: Off"; break;
    }
    lv_label_set_text(s_ctx.sky_label, sky_text);

    if (status->uvb_calibration.valid) {
        lv_spinbox_set_value(s_ctx.spin_calib_k, (int32_t)status->uvb_calibration.k);
        lv_spinbox_set_value(s_ctx.spin_calib_uvi, (int32_t)status->uvb_calibration.uvi_max);
        lv_label_set_text_fmt(s_ctx.label_calib_status, "Calibration k=%.2f uvi=%.2f", status->uvb_calibration.k, status->uvb_calibration.uvi_max);
    }

    s_ctx.updating_controls = false;
}

static void update_alarm_button(void)
{
    lv_label_set_text(s_ctx.btn_mute_label, s_ctx.alarm_muted ? "Alarme muette" : "Mute alarmes");
    if (s_ctx.alarm_muted) {
        lv_obj_add_state(s_ctx.btn_mute, LV_STATE_CHECKED);
    } else {
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
}

static void status_banner_async(void *param)
{
    status_banner_msg_t *msg = param;
    if (!msg) {
        return;
    }
    lvgl_port_lock();
    set_status_banner_locked(msg->text, msg->error);
    lvgl_port_unlock();
    lv_free(msg);
}

static void set_status_banner(const char *text, bool error)
{
    if (!s_ctx.label_status_banner || !text) {
        return;
    }
    status_banner_msg_t *msg = lv_malloc(sizeof(status_banner_msg_t));
    if (!msg) {
        ESP_LOGW(TAG, "Impossible d'afficher le message de statut");
        return;
    }
    msg->error = error;
    strlcpy(msg->text, text, sizeof(msg->text));
    lv_async_call(status_banner_async, msg);
}

static void set_status_banner_locked(const char *text, bool error)
{
    if (!s_ctx.label_status_banner || !text) {
        return;
    }
    lv_obj_set_style_text_color(s_ctx.label_status_banner,
                                lv_palette_main(error ? LV_PALETTE_RED : LV_PALETTE_BLUE), 0);
    lv_label_set_text(s_ctx.label_status_banner, text);
}

static void notify_errorf(const char *prefix, esp_err_t err)
{
    if (err == ESP_OK) {
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s (%s)", prefix ? prefix : "Erreur", esp_err_to_name(err));
    set_status_banner(buf, true);
    ESP_LOGW(TAG, "%s", buf);
}

static void send_light_command(void)
{
    terrarium_light_command_t cmd = {
        .cct_day = (uint16_t)lv_slider_get_value(s_ctx.sliders[SLIDER_CCT_DAY].slider),
        .cct_warm = (uint16_t)lv_slider_get_value(s_ctx.sliders[SLIDER_CCT_WARM].slider),
        .uva = (uint16_t)lv_slider_get_value(s_ctx.sliders[SLIDER_UVA].slider),
        .uvb = (uint16_t)lv_slider_get_value(s_ctx.sliders[SLIDER_UVB].slider),
        .uvb_period_s = (uint16_t)lv_spinbox_get_value(s_ctx.spin_uvb_period),
        .uvb_duty_pm = (uint16_t)lv_spinbox_get_value(s_ctx.spin_uvb_duty),
        .sky = s_ctx.last_status.dome.sky_mode,
    };
    esp_err_t err = network_manager_post_light(&cmd);
    if (err != ESP_OK) {
        notify_errorf("Commande éclairage", err);
    }
}

