/**
 * @file ui_climate.c
 * @brief Climate Control UI Implementation for LVGL
 * @version 1.0
 * @date 2026-01-06
 */

#include "ui_climate.h"
#include "climate_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_CLIMATE";

// ====================================================================================
// VARIABLES GLOBALES UI
// ====================================================================================

// Pages principales
static lv_obj_t *page_climate_dashboard = NULL;
static lv_obj_t *page_terrarium_detail = NULL;
static lv_obj_t *page_terrarium_settings = NULL;

// Settings page widgets
static lv_obj_t *settings_temp_hot_slider = NULL;
static lv_obj_t *settings_temp_cold_slider = NULL;
static lv_obj_t *settings_humidity_slider = NULL;
static lv_obj_t *settings_temp_hot_label = NULL;
static lv_obj_t *settings_temp_cold_label = NULL;
static lv_obj_t *settings_humidity_label = NULL;
static uint8_t settings_terrarium_id = 0;

// Numeric input popup for temperature (min/max)
static lv_obj_t *temp_input_popup = NULL;
static lv_obj_t *temp_input_min_ta = NULL;    // Min temperature textarea
static lv_obj_t *temp_input_max_ta = NULL;    // Max temperature textarea
static lv_obj_t *temp_input_active_ta = NULL; // Currently active textarea
static lv_obj_t *temp_input_target_label =
    NULL; // Label to update when confirmed
static lv_obj_t *temp_input_target_slider = NULL; // Slider to update
static int temp_input_min = 0;                    // Min allowed temperature
static int temp_input_max = 50;                   // Max allowed temperature
static uint8_t temp_input_zone = 0;               // 0=hot, 1=mid, 2=cold

// Dashboard widgets
static lv_obj_t *dashboard_container = NULL;
static lv_obj_t *terrarium_cards[MAX_TERRARIUMS] = {NULL};

// Détail terrarium widgets
static uint8_t current_terrarium_id = 0;
static lv_obj_t *detail_temp_hot_widget = NULL;
static lv_obj_t *detail_temp_cold_widget = NULL;
static lv_obj_t *detail_humidity_widget = NULL;
static lv_obj_t *detail_basin_widget = NULL;
static lv_obj_t *detail_reservoir_widget = NULL;
static lv_obj_t *detail_equipment_container = NULL;

// Alertes
static lv_obj_t *alerts_list = NULL;

// Parent screen
static lv_obj_t *ui_parent = NULL;

// Timer pour mise à jour
static lv_timer_t *update_timer = NULL;

// ====================================================================================
// STYLES
// ====================================================================================

static lv_style_t style_card;
static lv_style_t style_card_pressed;
static lv_style_t style_value_big;
static lv_style_t style_label_small;
static lv_style_t style_btn_on;
static lv_style_t style_btn_off;
static bool styles_initialized = false;

static void init_styles(void) {
  if (styles_initialized)
    return;

  // Card style
  lv_style_init(&style_card);
  lv_style_set_bg_color(&style_card, COLOR_CLIMATE_BG_CARD);
  lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
  lv_style_set_radius(&style_card, 16);
  lv_style_set_border_width(&style_card, 2);
  lv_style_set_border_color(&style_card, COLOR_CLIMATE_PRIMARY);
  lv_style_set_border_opa(&style_card, LV_OPA_50);
  lv_style_set_pad_all(&style_card, 12);
  lv_style_set_shadow_width(&style_card, 20);
  lv_style_set_shadow_color(&style_card, lv_color_black());
  lv_style_set_shadow_opa(&style_card, LV_OPA_20);

  // Card pressed
  lv_style_init(&style_card_pressed);
  lv_style_set_bg_color(&style_card_pressed, COLOR_CLIMATE_ACCENT);
  lv_style_set_transform_scale(&style_card_pressed, 256 * 0.98);

  // Big value text
  lv_style_init(&style_value_big);
  lv_style_set_text_font(&style_value_big, &lv_font_montserrat_28);
  lv_style_set_text_color(&style_value_big, lv_color_white());

  // Small label
  lv_style_init(&style_label_small);
  lv_style_set_text_font(&style_label_small, &lv_font_montserrat_12);
  lv_style_set_text_color(&style_label_small, lv_color_hex(0xA0A0A0));

  // Button ON
  lv_style_init(&style_btn_on);
  lv_style_set_bg_color(&style_btn_on, COLOR_EQUIP_ON);
  lv_style_set_bg_opa(&style_btn_on, LV_OPA_COVER);
  lv_style_set_radius(&style_btn_on, 8);

  // Button OFF
  lv_style_init(&style_btn_off);
  lv_style_set_bg_color(&style_btn_off, COLOR_EQUIP_OFF);
  lv_style_set_bg_opa(&style_btn_off, LV_OPA_COVER);
  lv_style_set_radius(&style_btn_off, 8);

  styles_initialized = true;
}

// ====================================================================================
// HELPERS
// ====================================================================================

static lv_color_t get_temp_color(float temp, float min, float max) {
  if (temp < min - 2)
    return COLOR_TEMP_COLD;
  if (temp > max + 2)
    return COLOR_TEMP_HOT;
  return COLOR_TEMP_GOOD;
}

static lv_color_t get_uv_zone_color(ferguson_zone_t zone) {
  switch (zone) {
  case FERGUSON_ZONE_1:
    return COLOR_UV_ZONE_1;
  case FERGUSON_ZONE_2:
    return COLOR_UV_ZONE_2;
  case FERGUSON_ZONE_3:
    return COLOR_UV_ZONE_3;
  case FERGUSON_ZONE_4:
    return COLOR_UV_ZONE_4;
  default:
    return COLOR_UV_ZONE_1;
  }
}

// ====================================================================================
// CALLBACKS
// ====================================================================================

static void terrarium_card_clicked_cb(lv_event_t *e) {
  lv_obj_t *card = lv_event_get_target(e);
  uint8_t id = (uint8_t)(uintptr_t)lv_obj_get_user_data(card);
  ESP_LOGI(TAG, "Terrarium card clicked: %d", id);
  ui_climate_show_terrarium(id);
}

// Forward declaration (defined later, public function)
// ui_climate_show_settings is now public - declared in ui_climate.h

// Settings button on card - opens terrarium SETTINGS page (not detail)
static void settings_btn_clicked_cb(lv_event_t *e) {
  // Stop event from propagating to parent card
  lv_event_stop_bubbling(e);
  lv_event_stop_processing(e);

  lv_obj_t *btn = lv_event_get_target(e);
  uint8_t id = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
  ESP_LOGI(TAG, "Terrarium settings clicked: %d", id);
  ui_climate_show_settings(id);
}

static void back_from_settings_cb(lv_event_t *e) {
  (void)e;
  ESP_LOGI(TAG, "Back from settings - returning to HOME");

  // Delete the settings page
  if (page_terrarium_settings) {
    lv_obj_del(page_terrarium_settings);
    page_terrarium_settings = NULL;
  }

  // Navigate back to HOME page (defined in main.c)
  // Use extern to call the navigation function from main.c
  extern void navigate_to_home_from_climate(void);
  navigate_to_home_from_climate();
}

static void back_to_dashboard_cb(lv_event_t *e) {
  (void)e;
  ui_climate_show_dashboard();
}

static void equipment_toggle_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  const char *equip_name = (const char *)lv_obj_get_user_data(btn);

  if (!equip_name)
    return;

  terrarium_config_t *t = climate_get_terrarium(current_terrarium_id);
  if (!t)
    return;

  // Toggle equipment
  if (strcmp(equip_name, "heating") == 0) {
    bool new_state = !t->equipment.heating_on;
    climate_set_heating(current_terrarium_id, new_state, new_state ? 100 : 0);
  } else if (strcmp(equip_name, "uv") == 0) {
    bool new_state = !t->equipment.uv_lamp_on;
    climate_set_uv_lamp(current_terrarium_id, new_state, new_state ? 100 : 0);
  } else if (strcmp(equip_name, "light") == 0) {
    climate_set_day_light(current_terrarium_id, !t->equipment.day_light_on);
  } else if (strcmp(equip_name, "misting") == 0) {
    if (!t->equipment.misting_on) {
      climate_trigger_misting(current_terrarium_id);
    }
  } else if (strcmp(equip_name, "pump") == 0) {
    climate_set_pump(current_terrarium_id, !t->equipment.pump_on);
  }

  // Update will happen on next timer tick
}

static void refill_water_cb(lv_event_t *e) {
  (void)e;
  climate_refill_water(current_terrarium_id, 100, 100);
  ESP_LOGI(TAG, "Water refilled for terrarium %d", current_terrarium_id);
}

static void add_terrarium_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  terrarium_type_t type =
      (terrarium_type_t)(uintptr_t)lv_obj_get_user_data(btn);

  int id = climate_add_terrarium(type, NULL);
  if (id >= 0) {
    ESP_LOGI(TAG, "Added terrarium type %d with ID %d", type, id);
    ui_climate_update_dashboard();
  }
}

static void show_schedule_cb(lv_event_t *e) {
  (void)e;
  ui_climate_show_schedule(current_terrarium_id);
}

static void show_alerts_cb(lv_event_t *e) {
  (void)e;
  ui_climate_show_alerts();
}

// ====================================================================================
// WIDGETS CREATION
// ====================================================================================

lv_obj_t *
ui_climate_create_terrarium_card(lv_obj_t *parent,
                                 const terrarium_config_t *terrarium) {
  // Card container
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 220, 180);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_add_style(card, &style_card_pressed, LV_STATE_PRESSED);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_user_data(card, (void *)(uintptr_t)terrarium->id);
  lv_obj_add_event_cb(card, terrarium_card_clicked_cb, LV_EVENT_CLICKED, NULL);

  // Type icon and name
  lv_obj_t *header = lv_obj_create(card);
  lv_obj_set_size(header, LV_PCT(100), 40);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  // Left part: Icon + Name
  lv_obj_t *left_group = lv_obj_create(header);
  lv_obj_set_size(left_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(left_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(left_group, 0, 0);
  lv_obj_set_style_pad_all(left_group, 0, 0);
  lv_obj_set_flex_flow(left_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(left_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(left_group, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  // Icon label (using text since LVGL doesn't have built-in terrarium icons)
  lv_obj_t *icon = lv_label_create(left_group);
  lv_label_set_text(icon, climate_get_type_icon(terrarium->type));
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);

  // Name
  lv_obj_t *name = lv_label_create(left_group);
  lv_label_set_text(name, terrarium->name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, lv_color_white(), 0);
  lv_obj_set_style_pad_left(name, 8, 0);

  // Settings button (gear icon) - top right
  lv_obj_t *settings_btn = lv_btn_create(header);
  lv_obj_set_size(settings_btn, 36, 36);
  lv_obj_set_style_bg_color(settings_btn, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_set_style_bg_color(settings_btn,
                            lv_color_darken(COLOR_CLIMATE_PRIMARY, LV_OPA_20),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(settings_btn, 18, 0);
  lv_obj_set_style_border_width(settings_btn, 0, 0);
  lv_obj_set_style_shadow_width(settings_btn, 6, 0);
  lv_obj_set_style_shadow_color(settings_btn, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_set_style_shadow_opa(settings_btn, LV_OPA_40, 0);
  lv_obj_set_user_data(settings_btn, (void *)(uintptr_t)terrarium->id);
  lv_obj_add_event_cb(settings_btn, settings_btn_clicked_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_add_flag(settings_btn, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *settings_icon = lv_label_create(settings_btn);
  lv_label_set_text(settings_icon, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(settings_icon, lv_color_hex(0x0A1510), 0);
  lv_obj_set_style_text_font(settings_icon, &lv_font_montserrat_16, 0);
  lv_obj_center(settings_icon);

  // Temperature display
  lv_obj_t *temp_row = lv_obj_create(card);
  lv_obj_set_size(temp_row, LV_PCT(100), 50);
  lv_obj_set_style_bg_opa(temp_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(temp_row, 0, 0);
  lv_obj_set_style_pad_all(temp_row, 0, 0);
  lv_obj_align(temp_row, LV_ALIGN_TOP_MID, 0, 45);
  lv_obj_set_flex_flow(temp_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(temp_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Hot zone
  lv_obj_t *hot_container = lv_obj_create(temp_row);
  lv_obj_set_size(hot_container, 90, 45);
  lv_obj_set_style_bg_color(hot_container, COLOR_TEMP_HOT, 0);
  lv_obj_set_style_bg_opa(hot_container, LV_OPA_20, 0);
  lv_obj_set_style_radius(hot_container, 8, 0);
  lv_obj_set_style_border_width(hot_container, 0, 0);
  lv_obj_set_style_pad_all(hot_container, 4, 0);

  char temp_str[16];
  snprintf(temp_str, sizeof(temp_str), "%.1f°C",
           terrarium->sensors.temp_hot_zone);
  lv_obj_t *hot_val = lv_label_create(hot_container);
  lv_label_set_text(hot_val, temp_str);
  lv_obj_set_style_text_font(hot_val, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(hot_val, COLOR_TEMP_HOT, 0);
  lv_obj_align(hot_val, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *hot_label = lv_label_create(hot_container);
  lv_label_set_text(hot_label, "Chaud");
  lv_obj_add_style(hot_label, &style_label_small, 0);
  lv_obj_align(hot_label, LV_ALIGN_BOTTOM_MID, 0, 2);

  // Cold zone
  lv_obj_t *cold_container = lv_obj_create(temp_row);
  lv_obj_set_size(cold_container, 90, 45);
  lv_obj_set_style_bg_color(cold_container, COLOR_TEMP_COLD, 0);
  lv_obj_set_style_bg_opa(cold_container, LV_OPA_20, 0);
  lv_obj_set_style_radius(cold_container, 8, 0);
  lv_obj_set_style_border_width(cold_container, 0, 0);
  lv_obj_set_style_pad_all(cold_container, 4, 0);

  snprintf(temp_str, sizeof(temp_str), "%.1f°C",
           terrarium->sensors.temp_cold_zone);
  lv_obj_t *cold_val = lv_label_create(cold_container);
  lv_label_set_text(cold_val, temp_str);
  lv_obj_set_style_text_font(cold_val, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(cold_val, COLOR_TEMP_COLD, 0);
  lv_obj_align(cold_val, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *cold_label = lv_label_create(cold_container);
  lv_label_set_text(cold_label, "Froid");
  lv_obj_add_style(cold_label, &style_label_small, 0);
  lv_obj_align(cold_label, LV_ALIGN_BOTTOM_MID, 0, 2);

  // Bottom row: Humidity + UV + Status
  lv_obj_t *bottom_row = lv_obj_create(card);
  lv_obj_set_size(bottom_row, LV_PCT(100), 40);
  lv_obj_set_style_bg_opa(bottom_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bottom_row, 0, 0);
  lv_obj_set_style_pad_all(bottom_row, 0, 0);
  lv_obj_align(bottom_row, LV_ALIGN_TOP_MID, 0, 100);
  lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Humidity
  char hum_str[16];
  snprintf(hum_str, sizeof(hum_str), LV_SYMBOL_REFRESH " %.0f%%",
           terrarium->sensors.humidity);
  lv_obj_t *hum_label = lv_label_create(bottom_row);
  lv_label_set_text(hum_label, hum_str);
  lv_obj_set_style_text_color(hum_label, COLOR_HUMIDITY, 0);

  // UV Zone
  char uv_str[16];
  snprintf(uv_str, sizeof(uv_str), "UV Z%d", terrarium->uv_zone);
  lv_obj_t *uv_label = lv_label_create(bottom_row);
  lv_label_set_text(uv_label, uv_str);
  lv_obj_set_style_text_color(uv_label, get_uv_zone_color(terrarium->uv_zone),
                              0);

  // Equipment status icons
  lv_obj_t *status_row = lv_obj_create(bottom_row);
  lv_obj_set_size(status_row, 80, 30);
  lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(status_row, 0, 0);
  lv_obj_set_style_pad_all(status_row, 0, 0);
  lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // Heating indicator
  lv_obj_t *heat_ind = lv_label_create(status_row);
  lv_label_set_text(heat_ind, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(
      heat_ind,
      terrarium->equipment.heating_on ? COLOR_EQUIP_ON : COLOR_EQUIP_OFF, 0);

  // Light indicator
  lv_obj_t *light_ind = lv_label_create(status_row);
  lv_label_set_text(light_ind, LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(
      light_ind,
      terrarium->equipment.day_light_on ? COLOR_EQUIP_ON : COLOR_EQUIP_OFF, 0);
  lv_obj_set_style_pad_left(light_ind, 4, 0);

  // Alert indicator
  uint8_t alert_count = climate_get_active_alert_count();
  if (alert_count > 0) {
    lv_obj_t *alert_ind = lv_label_create(status_row);
    lv_label_set_text(alert_ind, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(alert_ind, COLOR_ALERT_WARNING, 0);
    lv_obj_set_style_pad_left(alert_ind, 4, 0);
  }

  return card;
}

lv_obj_t *ui_climate_create_temp_widget(lv_obj_t *parent,
                                        const char *label_text,
                                        bool is_hot_zone) {
  lv_obj_t *container = lv_obj_create(parent);
  lv_obj_set_size(container, 150, 150);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);

  // Arc for temperature
  lv_obj_t *arc = lv_arc_create(container);
  lv_obj_set_size(arc, 130, 130);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 60); // 0-60°C range
  lv_arc_set_value(arc, 25);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

  lv_color_t arc_color = is_hot_zone ? COLOR_TEMP_HOT : COLOR_TEMP_COLD;
  lv_obj_set_style_arc_color(arc, arc_color, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);

  // Value label
  lv_obj_t *value = lv_label_create(container);
  lv_label_set_text(value, "25.0°C");
  lv_obj_add_style(value, &style_value_big, 0);
  lv_obj_align(value, LV_ALIGN_CENTER, 0, -5);

  // Label
  lv_obj_t *label = lv_label_create(container);
  lv_label_set_text(label, label_text);
  lv_obj_add_style(label, &style_label_small, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 25);

  return container;
}

lv_obj_t *ui_climate_create_humidity_widget(lv_obj_t *parent) {
  lv_obj_t *container = lv_obj_create(parent);
  lv_obj_set_size(container, 120, 150);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);

  // Bar for humidity
  lv_obj_t *bar = lv_bar_create(container);
  lv_obj_set_size(bar, 30, 100);
  lv_obj_align(bar, LV_ALIGN_CENTER, 0, -10);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 50, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x333333), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, COLOR_HUMIDITY, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);

  // Value
  lv_obj_t *value = lv_label_create(container);
  lv_label_set_text(value, "50%");
  lv_obj_set_style_text_font(value, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(value, COLOR_HUMIDITY, 0);
  lv_obj_align(value, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Icon
  lv_obj_t *icon = lv_label_create(container);
  lv_label_set_text(icon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_color(icon, COLOR_HUMIDITY, 0);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);

  return container;
}

lv_obj_t *ui_climate_create_equipment_btn(lv_obj_t *parent,
                                          const char *icon_text,
                                          const char *label_text, bool is_on) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 100, 60);
  lv_obj_add_style(btn, is_on ? &style_btn_on : &style_btn_off, 0);

  lv_obj_t *content = lv_obj_create(btn);
  lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon = lv_label_create(content);
  lv_label_set_text(icon, icon_text);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);

  lv_obj_t *label = lv_label_create(content);
  lv_label_set_text(label, label_text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);

  return btn;
}

lv_obj_t *ui_climate_create_water_level_widget(lv_obj_t *parent,
                                               const char *label_text) {
  lv_obj_t *container = lv_obj_create(parent);
  lv_obj_set_size(container, 80, 120);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);

  // Bar
  lv_obj_t *bar = lv_bar_create(container);
  lv_obj_set_size(bar, 40, 80);
  lv_obj_align(bar, LV_ALIGN_CENTER, 0, -5);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 75, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x333333), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);

  // Value
  lv_obj_t *value = lv_label_create(container);
  lv_label_set_text(value, "75%");
  lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(value, lv_color_white(), 0);
  lv_obj_align(value, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Label
  lv_obj_t *label = lv_label_create(container);
  lv_label_set_text(label, label_text);
  lv_obj_add_style(label, &style_label_small, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

  return container;
}

// ====================================================================================
// PAGE CREATION
// ====================================================================================

lv_obj_t *ui_climate_create_dashboard(lv_obj_t *parent) {
  page_climate_dashboard = lv_obj_create(parent);
  lv_obj_set_size(page_climate_dashboard, 1024,
                  540); // Adjust for 7" screen minus status bar
  lv_obj_set_pos(page_climate_dashboard, 0, 50);
  lv_obj_set_style_bg_color(page_climate_dashboard, COLOR_CLIMATE_BG_DARK, 0);
  lv_obj_set_style_border_width(page_climate_dashboard, 0, 0);
  lv_obj_set_style_radius(page_climate_dashboard, 0, 0);
  lv_obj_set_style_pad_all(page_climate_dashboard, 15, 0);

  // Header
  lv_obj_t *header = lv_label_create(page_climate_dashboard);
  lv_label_set_text(header, "🌡️ Gestion Climatique");
  lv_obj_set_style_text_font(header, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(header, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

  // Alert badge
  uint8_t alert_count = climate_get_active_alert_count();
  if (alert_count > 0) {
    lv_obj_t *badge = lv_label_create(page_climate_dashboard);
    char badge_str[16];
    snprintf(badge_str, sizeof(badge_str), LV_SYMBOL_WARNING " %d alertes",
             alert_count);
    lv_label_set_text(badge, badge_str);
    lv_obj_set_style_text_color(badge, COLOR_ALERT_WARNING, 0);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, 0, 5);
  }

  // Container for terrarium cards
  dashboard_container = lv_obj_create(page_climate_dashboard);
  lv_obj_set_size(dashboard_container, LV_PCT(100), 420);
  lv_obj_align(dashboard_container, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_opa(dashboard_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dashboard_container, 0, 0);
  lv_obj_set_style_pad_all(dashboard_container, 0, 0);
  lv_obj_set_flex_flow(dashboard_container, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(dashboard_container, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(dashboard_container, 15, 0);
  lv_obj_set_style_pad_column(dashboard_container, 15, 0);
  lv_obj_set_scroll_dir(dashboard_container, LV_DIR_VER);

  // Add terrarium cards
  for (int i = 0; i < climate_get_terrarium_count(); i++) {
    terrarium_config_t *t = climate_get_terrarium(i);
    if (t && t->active) {
      terrarium_cards[i] =
          ui_climate_create_terrarium_card(dashboard_container, t);
    }
  }

  // Add new terrarium button
  lv_obj_t *add_btn = lv_btn_create(dashboard_container);
  lv_obj_set_size(add_btn, 220, 180);
  lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_border_width(add_btn, 2, 0);
  lv_obj_set_style_border_color(add_btn, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_set_style_border_opa(add_btn, LV_OPA_30, 0);
  lv_obj_set_style_radius(add_btn, 16, 0);

  lv_obj_t *add_icon = lv_label_create(add_btn);
  lv_label_set_text(add_icon, LV_SYMBOL_PLUS);
  lv_obj_set_style_text_font(add_icon, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(add_icon, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_align(add_icon, LV_ALIGN_CENTER, 0, -15);

  lv_obj_t *add_label = lv_label_create(add_btn);
  lv_label_set_text(add_label, "Ajouter Terrarium");
  lv_obj_set_style_text_color(add_label, lv_color_hex(0x808080), 0);
  lv_obj_align(add_label, LV_ALIGN_CENTER, 0, 30);

  // For now, add as desert type - later show type selection popup
  lv_obj_set_user_data(add_btn, (void *)(uintptr_t)TERRARIUM_DESERT);
  lv_obj_add_event_cb(add_btn, add_terrarium_cb, LV_EVENT_CLICKED, NULL);

  return page_climate_dashboard;
}

lv_obj_t *ui_climate_create_terrarium_detail(lv_obj_t *parent) {
  page_terrarium_detail = lv_obj_create(parent);
  lv_obj_set_size(page_terrarium_detail, 1024, 540);
  lv_obj_set_pos(page_terrarium_detail, 0, 50);
  lv_obj_set_style_bg_color(page_terrarium_detail, COLOR_CLIMATE_BG_DARK, 0);
  lv_obj_set_style_border_width(page_terrarium_detail, 0, 0);
  lv_obj_set_style_pad_all(page_terrarium_detail, 15, 0);
  lv_obj_add_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);

  // Back button
  lv_obj_t *back_btn = lv_btn_create(page_terrarium_detail);
  lv_obj_set_size(back_btn, 100, 40);
  lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_CLIMATE_ACCENT, 0);
  lv_obj_add_event_cb(back_btn, back_to_dashboard_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_label);

  // Title (will be updated)
  lv_obj_t *title = lv_label_create(page_terrarium_detail);
  lv_label_set_text(title, "Terrarium");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  // Sensors row
  lv_obj_t *sensors_row = lv_obj_create(page_terrarium_detail);
  lv_obj_set_size(sensors_row, LV_PCT(100), 170);
  lv_obj_align(sensors_row, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_opa(sensors_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sensors_row, 0, 0);
  lv_obj_set_flex_flow(sensors_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sensors_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Temperature widgets
  detail_temp_hot_widget =
      ui_climate_create_temp_widget(sensors_row, "Zone Chaude", true);
  detail_temp_cold_widget =
      ui_climate_create_temp_widget(sensors_row, "Zone Froide", false);

  // Humidity widget
  detail_humidity_widget = ui_climate_create_humidity_widget(sensors_row);

  // Water levels
  detail_basin_widget =
      ui_climate_create_water_level_widget(sensors_row, "Bassin");
  detail_reservoir_widget =
      ui_climate_create_water_level_widget(sensors_row, "Réservoir");

  // Equipment row
  detail_equipment_container = lv_obj_create(page_terrarium_detail);
  lv_obj_set_size(detail_equipment_container, LV_PCT(100), 100);
  lv_obj_align(detail_equipment_container, LV_ALIGN_TOP_MID, 0, 230);
  lv_obj_set_style_bg_color(detail_equipment_container, COLOR_CLIMATE_BG_CARD,
                            0);
  lv_obj_set_style_radius(detail_equipment_container, 12, 0);
  lv_obj_set_style_border_width(detail_equipment_container, 0, 0);
  lv_obj_set_flex_flow(detail_equipment_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(detail_equipment_container, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(detail_equipment_container, 10, 0);

  // Equipment buttons
  lv_obj_t *heat_btn = ui_climate_create_equipment_btn(
      detail_equipment_container, LV_SYMBOL_CHARGE, "Chauffage", false);
  lv_obj_set_user_data(heat_btn, (void *)"heating");
  lv_obj_add_event_cb(heat_btn, equipment_toggle_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *uv_btn = ui_climate_create_equipment_btn(
      detail_equipment_container, LV_SYMBOL_IMAGE, "UV", false);
  lv_obj_set_user_data(uv_btn, (void *)"uv");
  lv_obj_add_event_cb(uv_btn, equipment_toggle_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *light_btn = ui_climate_create_equipment_btn(
      detail_equipment_container, LV_SYMBOL_EYE_OPEN, "Lumière", false);
  lv_obj_set_user_data(light_btn, (void *)"light");
  lv_obj_add_event_cb(light_btn, equipment_toggle_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *mist_btn = ui_climate_create_equipment_btn(
      detail_equipment_container, LV_SYMBOL_REFRESH, "Brumiser", false);
  lv_obj_set_user_data(mist_btn, (void *)"misting");
  lv_obj_add_event_cb(mist_btn, equipment_toggle_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *pump_btn = ui_climate_create_equipment_btn(
      detail_equipment_container, LV_SYMBOL_LOOP, "Pompe", false);
  lv_obj_set_user_data(pump_btn, (void *)"pump");
  lv_obj_add_event_cb(pump_btn, equipment_toggle_cb, LV_EVENT_CLICKED, NULL);

  // Bottom action bar
  lv_obj_t *action_bar = lv_obj_create(page_terrarium_detail);
  lv_obj_set_size(action_bar, LV_PCT(100), 60);
  lv_obj_align(action_bar, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(action_bar, COLOR_CLIMATE_BG_CARD, 0);
  lv_obj_set_style_radius(action_bar, 12, 0);
  lv_obj_set_style_border_width(action_bar, 0, 0);
  lv_obj_set_flex_flow(action_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(action_bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(action_bar, 5, 0);

  // Schedule button
  lv_obj_t *schedule_btn = lv_btn_create(action_bar);
  lv_obj_set_size(schedule_btn, 180, 45);
  lv_obj_set_style_bg_color(schedule_btn, COLOR_CLIMATE_ACCENT, 0);
  lv_obj_set_style_radius(schedule_btn, 10, 0);
  lv_obj_add_event_cb(schedule_btn, show_schedule_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *schedule_label = lv_label_create(schedule_btn);
  lv_label_set_text(schedule_label, LV_SYMBOL_SETTINGS " Programmation");
  lv_obj_center(schedule_label);

  // Alerts button
  lv_obj_t *alert_btn = lv_btn_create(action_bar);
  lv_obj_set_size(alert_btn, 140, 45);
  lv_obj_set_style_bg_color(alert_btn, COLOR_ALERT_WARNING, 0);
  lv_obj_set_style_bg_opa(alert_btn, LV_OPA_80, 0);
  lv_obj_set_style_radius(alert_btn, 10, 0);
  lv_obj_add_event_cb(alert_btn, show_alerts_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *alert_label = lv_label_create(alert_btn);
  lv_label_set_text(alert_label, LV_SYMBOL_WARNING " Alertes");
  lv_obj_center(alert_label);

  // Refill water button
  lv_obj_t *refill_btn = lv_btn_create(action_bar);
  lv_obj_set_size(refill_btn, 150, 45);
  lv_obj_set_style_bg_color(refill_btn, COLOR_HUMIDITY, 0);
  lv_obj_set_style_radius(refill_btn, 10, 0);
  lv_obj_add_event_cb(refill_btn, refill_water_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *refill_label = lv_label_create(refill_btn);
  lv_label_set_text(refill_label, LV_SYMBOL_PLUS " Remplir eau");
  lv_obj_center(refill_label);

  return page_terrarium_detail;
}

// ====================================================================================
// PAGE: PROGRAMMATION HORAIRE
// ====================================================================================

static lv_obj_t *page_schedule_detail = NULL;
static uint8_t schedule_terrarium_id = 0;

static void back_to_detail_cb(lv_event_t *e) {
  (void)e;
  if (page_schedule_detail) {
    lv_obj_add_flag(page_schedule_detail, LV_OBJ_FLAG_HIDDEN);
  }
  if (page_terrarium_detail) {
    lv_obj_clear_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);
  }
}

static lv_obj_t *create_schedule_row(lv_obj_t *parent, const char *title,
                                     schedule_t *schedule,
                                     const char *schedule_id) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 80);
  lv_obj_set_style_bg_color(row, COLOR_CLIMATE_BG_CARD, 0);
  lv_obj_set_style_radius(row, 12, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 10, 0);

  // Title
  lv_obj_t *label = lv_label_create(row);
  lv_label_set_text(label, title);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, -15);

  // ON time display
  char on_str[32];
  snprintf(on_str, sizeof(on_str), "ON: %02d:%02d", schedule->on_hour,
           schedule->on_minute);
  lv_obj_t *on_label = lv_label_create(row);
  lv_label_set_text(on_label, on_str);
  lv_obj_set_style_text_color(on_label, COLOR_EQUIP_ON, 0);
  lv_obj_align(on_label, LV_ALIGN_LEFT_MID, 5, 15);

  // OFF time display
  char off_str[32];
  snprintf(off_str, sizeof(off_str), "OFF: %02d:%02d", schedule->off_hour,
           schedule->off_minute);
  lv_obj_t *off_label = lv_label_create(row);
  lv_label_set_text(off_label, off_str);
  lv_obj_set_style_text_color(off_label, COLOR_EQUIP_OFF, 0);
  lv_obj_align(off_label, LV_ALIGN_LEFT_MID, 120, 15);

  // Enable/Disable switch
  lv_obj_t *sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_bg_color(sw, COLOR_CLIMATE_ACCENT,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (schedule->enabled) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }

  return row;
}

lv_obj_t *ui_climate_create_schedule_page(lv_obj_t *parent) {
  page_schedule_detail = lv_obj_create(parent);
  lv_obj_set_size(page_schedule_detail, 1024, 540);
  lv_obj_set_pos(page_schedule_detail, 0, 50);
  lv_obj_set_style_bg_color(page_schedule_detail, COLOR_CLIMATE_BG_DARK, 0);
  lv_obj_set_style_border_width(page_schedule_detail, 0, 0);
  lv_obj_set_style_pad_all(page_schedule_detail, 15, 0);
  lv_obj_add_flag(page_schedule_detail, LV_OBJ_FLAG_HIDDEN);

  // Back button
  lv_obj_t *back_btn = lv_btn_create(page_schedule_detail);
  lv_obj_set_size(back_btn, 100, 40);
  lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_CLIMATE_ACCENT, 0);
  lv_obj_add_event_cb(back_btn, back_to_detail_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_label);

  // Title
  lv_obj_t *title = lv_label_create(page_schedule_detail);
  lv_label_set_text(title, LV_SYMBOL_SETTINGS " Programmation Horaire");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  // Scrollable container for schedules
  lv_obj_t *scroll_container = lv_obj_create(page_schedule_detail);
  lv_obj_set_size(scroll_container, LV_PCT(100), 440);
  lv_obj_align(scroll_container, LV_ALIGN_TOP_MID, 0, 60);
  lv_obj_set_style_bg_opa(scroll_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroll_container, 0, 0);
  lv_obj_set_flex_flow(scroll_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(scroll_container, 10, 0);
  lv_obj_set_scroll_dir(scroll_container, LV_DIR_VER);

  return page_schedule_detail;
}

void ui_climate_show_schedule(uint8_t terrarium_id) {
  schedule_terrarium_id = terrarium_id;

  terrarium_config_t *t = climate_get_terrarium(terrarium_id);
  if (!t)
    return;

  if (!page_schedule_detail) {
    ui_climate_create_schedule_page(ui_parent);
  }

  // Clear and recreate schedule rows
  lv_obj_t *scroll =
      lv_obj_get_child(page_schedule_detail, 2); // Skip back btn and title
  if (scroll) {
    lv_obj_clean(scroll);

    create_schedule_row(scroll, LV_SYMBOL_EYE_OPEN " Éclairage Jour",
                        &t->light_schedule, "light");
    create_schedule_row(scroll, LV_SYMBOL_IMAGE " Lampe UV", &t->uv_schedule,
                        "uv");
    create_schedule_row(scroll, LV_SYMBOL_CHARGE " Chauffage",
                        &t->heating_schedule, "heating");
    create_schedule_row(scroll, LV_SYMBOL_LOOP " Pompe Cascade",
                        &t->pump_schedule, "pump");

    // Misting schedule (different format)
    lv_obj_t *mist_row = lv_obj_create(scroll);
    lv_obj_set_size(mist_row, LV_PCT(100), 100);
    lv_obj_set_style_bg_color(mist_row, COLOR_CLIMATE_BG_CARD, 0);
    lv_obj_set_style_radius(mist_row, 12, 0);
    lv_obj_set_style_border_width(mist_row, 0, 0);
    lv_obj_set_style_pad_all(mist_row, 10, 0);

    lv_obj_t *mist_title = lv_label_create(mist_row);
    lv_label_set_text(mist_title, LV_SYMBOL_REFRESH " Brumisation");
    lv_obj_set_style_text_font(mist_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mist_title, lv_color_white(), 0);
    lv_obj_align(mist_title, LV_ALIGN_LEFT_MID, 5, -25);

    char mist_str[64];
    snprintf(mist_str, sizeof(mist_str), "Intervalle: %d min | Durée: %d sec",
             t->misting.interval_minutes, t->misting.duration_seconds);
    lv_obj_t *mist_info = lv_label_create(mist_row);
    lv_label_set_text(mist_info, mist_str);
    lv_obj_set_style_text_color(mist_info, COLOR_HUMIDITY, 0);
    lv_obj_align(mist_info, LV_ALIGN_LEFT_MID, 5, 0);

    char mist_time_str[48];
    snprintf(mist_time_str, sizeof(mist_time_str), "Actif: %02d:00 - %02d:00",
             t->misting.start_hour, t->misting.end_hour);
    lv_obj_t *mist_time = lv_label_create(mist_row);
    lv_label_set_text(mist_time, mist_time_str);
    lv_obj_set_style_text_color(mist_time, lv_color_hex(0xA0A0A0), 0);
    lv_obj_align(mist_time, LV_ALIGN_LEFT_MID, 5, 25);

    lv_obj_t *mist_sw = lv_switch_create(mist_row);
    lv_obj_align(mist_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(mist_sw, COLOR_CLIMATE_ACCENT,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (t->misting.enabled) {
      lv_obj_add_state(mist_sw, LV_STATE_CHECKED);
    }
  }

  // Hide other pages, show this one
  if (page_terrarium_detail)
    lv_obj_add_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);
  if (page_climate_dashboard)
    lv_obj_add_flag(page_climate_dashboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(page_schedule_detail, LV_OBJ_FLAG_HIDDEN);
}

// ====================================================================================
// PAGE: ALERTES
// ====================================================================================

static lv_obj_t *page_alerts_list = NULL;

static void ack_alert_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  uint8_t alert_id = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
  climate_acknowledge_alert(alert_id);
  ESP_LOGI(TAG, "Alert %d acknowledged", alert_id);
  ui_climate_show_alerts(); // Refresh
}

lv_obj_t *ui_climate_create_alerts_page(lv_obj_t *parent) {
  page_alerts_list = lv_obj_create(parent);
  lv_obj_set_size(page_alerts_list, 1024, 540);
  lv_obj_set_pos(page_alerts_list, 0, 50);
  lv_obj_set_style_bg_color(page_alerts_list, COLOR_CLIMATE_BG_DARK, 0);
  lv_obj_set_style_border_width(page_alerts_list, 0, 0);
  lv_obj_set_style_pad_all(page_alerts_list, 15, 0);
  lv_obj_add_flag(page_alerts_list, LV_OBJ_FLAG_HIDDEN);

  // Back button
  lv_obj_t *back_btn = lv_btn_create(page_alerts_list);
  lv_obj_set_size(back_btn, 100, 40);
  lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_CLIMATE_ACCENT, 0);
  lv_obj_add_event_cb(back_btn, back_to_dashboard_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_label);

  // Title
  lv_obj_t *title = lv_label_create(page_alerts_list);
  lv_label_set_text(title, LV_SYMBOL_WARNING " Alertes Système");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, COLOR_ALERT_WARNING, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  // Scrollable list
  alerts_list = lv_obj_create(page_alerts_list);
  lv_obj_set_size(alerts_list, LV_PCT(100), 440);
  lv_obj_align(alerts_list, LV_ALIGN_TOP_MID, 0, 60);
  lv_obj_set_style_bg_opa(alerts_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(alerts_list, 0, 0);
  lv_obj_set_flex_flow(alerts_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(alerts_list, 8, 0);
  lv_obj_set_scroll_dir(alerts_list, LV_DIR_VER);

  return page_alerts_list;
}

void ui_climate_show_alerts(void) {
  if (!page_alerts_list) {
    ui_climate_create_alerts_page(ui_parent);
  }

  // Clear and recreate alert rows
  if (alerts_list) {
    lv_obj_clean(alerts_list);

    alert_t active_alerts[MAX_ALERTS];
    uint8_t count = climate_get_active_alerts(active_alerts);

    if (count == 0) {
      lv_obj_t *no_alerts = lv_label_create(alerts_list);
      lv_label_set_text(no_alerts, LV_SYMBOL_OK " Aucune alerte active");
      lv_obj_set_style_text_font(no_alerts, &lv_font_montserrat_20, 0);
      lv_obj_set_style_text_color(no_alerts, COLOR_EQUIP_ON, 0);
      lv_obj_align(no_alerts, LV_ALIGN_CENTER, 0, 0);
    } else {
      for (int i = 0; i < count; i++) {
        alert_t *a = &active_alerts[i];

        // Alert row
        lv_obj_t *row = lv_obj_create(alerts_list);
        lv_obj_set_size(row, LV_PCT(100), 70);

        // Color based on priority
        lv_color_t bg_color;
        switch (a->priority) {
        case ALERT_PRIORITY_CRITICAL:
          bg_color = lv_color_hex(0x4A1515);
          break;
        case ALERT_PRIORITY_WARNING:
          bg_color = lv_color_hex(0x4A3515);
          break;
        default:
          bg_color = COLOR_CLIMATE_BG_CARD;
          break;
        }
        lv_obj_set_style_bg_color(row, bg_color, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 10, 0);

        // Icon based on type
        const char *icon;
        switch (a->type) {
        case ALERT_TEMP_HIGH:
        case ALERT_TEMP_LOW:
          icon = LV_SYMBOL_WARNING;
          break;
        case ALERT_HUMIDITY_HIGH:
        case ALERT_HUMIDITY_LOW:
          icon = LV_SYMBOL_REFRESH;
          break;
        case ALERT_WATER_BASIN_LOW:
        case ALERT_WATER_RESERVOIR_LOW:
          icon = LV_SYMBOL_DOWNLOAD;
          break;
        case ALERT_EQUIPMENT_FAILURE:
          icon = LV_SYMBOL_CLOSE;
          break;
        default:
          icon = LV_SYMBOL_WARNING;
          break;
        }

        lv_obj_t *icon_label = lv_label_create(row);
        lv_label_set_text(icon_label, icon);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(icon_label,
                                    a->priority == ALERT_PRIORITY_CRITICAL
                                        ? COLOR_ALERT_CRITICAL
                                        : COLOR_ALERT_WARNING,
                                    0);
        lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 5, 0);

        // Message
        lv_obj_t *msg = lv_label_create(row);
        lv_label_set_text(msg, a->message);
        lv_obj_set_style_text_color(msg, lv_color_white(), 0);
        lv_obj_align(msg, LV_ALIGN_LEFT_MID, 40, -10);

        // Terrarium info
        terrarium_config_t *t = climate_get_terrarium(a->terrarium_id);
        if (t) {
          char terra_str[48];
          snprintf(terra_str, sizeof(terra_str), "Terrarium: %s", t->name);
          lv_obj_t *terra_label = lv_label_create(row);
          lv_label_set_text(terra_label, terra_str);
          lv_obj_set_style_text_color(terra_label, lv_color_hex(0x808080), 0);
          lv_obj_set_style_text_font(terra_label, &lv_font_montserrat_12, 0);
          lv_obj_align(terra_label, LV_ALIGN_LEFT_MID, 40, 15);
        }

        // Acknowledge button
        if (!a->acknowledged) {
          lv_obj_t *ack_btn = lv_btn_create(row);
          lv_obj_set_size(ack_btn, 80, 35);
          lv_obj_align(ack_btn, LV_ALIGN_RIGHT_MID, -5, 0);
          lv_obj_set_style_bg_color(ack_btn, COLOR_CLIMATE_ACCENT, 0);
          lv_obj_set_user_data(ack_btn, (void *)(uintptr_t)a->id);
          lv_obj_add_event_cb(ack_btn, ack_alert_cb, LV_EVENT_CLICKED, NULL);

          lv_obj_t *ack_label = lv_label_create(ack_btn);
          lv_label_set_text(ack_label, LV_SYMBOL_OK);
          lv_obj_center(ack_label);
        } else {
          lv_obj_t *acked = lv_label_create(row);
          lv_label_set_text(acked, LV_SYMBOL_OK " OK");
          lv_obj_set_style_text_color(acked, COLOR_EQUIP_ON, 0);
          lv_obj_align(acked, LV_ALIGN_RIGHT_MID, -10, 0);
        }
      }
    }
  }

  // Show page
  if (page_climate_dashboard)
    lv_obj_add_flag(page_climate_dashboard, LV_OBJ_FLAG_HIDDEN);
  if (page_terrarium_detail)
    lv_obj_add_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);
  if (page_schedule_detail)
    lv_obj_add_flag(page_schedule_detail, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(page_alerts_list, LV_OBJ_FLAG_HIDDEN);
}

// ====================================================================================
// POPUP: SÉLECTION TYPE TERRARIUM
// ====================================================================================

static lv_obj_t *popup_type_select = NULL;

static void type_select_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  terrarium_type_t type =
      (terrarium_type_t)(uintptr_t)lv_obj_get_user_data(btn);

  int id = climate_add_terrarium(type, NULL);
  if (id >= 0) {
    ESP_LOGI(TAG, "Added terrarium type %d with ID %d", type, id);
    ui_climate_update_dashboard();
  }

  // Hide popup
  if (popup_type_select) {
    lv_obj_add_flag(popup_type_select, LV_OBJ_FLAG_HIDDEN);
  }
}

static void close_popup_cb(lv_event_t *e) {
  (void)e;
  if (popup_type_select) {
    lv_obj_add_flag(popup_type_select, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_climate_show_type_selection(void) {
  if (!popup_type_select) {
    // Create popup
    popup_type_select = lv_obj_create(ui_parent);
    lv_obj_set_size(popup_type_select, 500, 450);
    lv_obj_center(popup_type_select);
    lv_obj_set_style_bg_color(popup_type_select, COLOR_CLIMATE_BG_CARD, 0);
    lv_obj_set_style_radius(popup_type_select, 20, 0);
    lv_obj_set_style_border_width(popup_type_select, 2, 0);
    lv_obj_set_style_border_color(popup_type_select, COLOR_CLIMATE_PRIMARY, 0);
    lv_obj_set_style_shadow_width(popup_type_select, 40, 0);
    lv_obj_set_style_shadow_color(popup_type_select, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(popup_type_select, LV_OPA_50, 0);

    // Close button
    lv_obj_t *close_btn = lv_btn_create(popup_type_select);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(close_btn, 20, 0);
    lv_obj_add_event_cb(close_btn, close_popup_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);

    // Title
    lv_obj_t *title = lv_label_create(popup_type_select);
    lv_label_set_text(title, "Choisir le type de terrarium");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_CLIMATE_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Type buttons grid
    lv_obj_t *grid = lv_obj_create(popup_type_select);
    lv_obj_set_size(grid, 460, 350);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 15, 0);
    lv_obj_set_style_pad_row(grid, 15, 0);

    // Create button for each terrarium type
    const struct {
      terrarium_type_t type;
      const char *name;
      const char *icon;
      const char *examples;
      lv_color_t color;
    } types[] = {
        {TERRARIUM_DESERT, "Désertique", LV_SYMBOL_CHARGE, "Pogona, Uromastyx",
         lv_color_hex(0xE67E22)},
        {TERRARIUM_SEMI_DESERT, "Semi-Désertique", LV_SYMBOL_EYE_OPEN,
         "Python royal, Serpent des blés", lv_color_hex(0xD4AC0D)},
        {TERRARIUM_TROPICAL, "Tropical", LV_SYMBOL_REFRESH,
         "Python vert, Dendrobates", lv_color_hex(0x27AE60)},
        {TERRARIUM_SEMI_TROPICAL, "Semi-Tropical", LV_SYMBOL_LOOP,
         "Boa, Gecko à crête", lv_color_hex(0x2980B9)},
    };

    for (int i = 0; i < 4; i++) {
      lv_obj_t *btn = lv_btn_create(grid);
      lv_obj_set_size(btn, 210, 150);
      lv_obj_set_style_bg_color(btn, types[i].color, 0);
      lv_obj_set_style_bg_opa(btn, LV_OPA_30, 0);
      lv_obj_set_style_radius(btn, 15, 0);
      lv_obj_set_style_border_width(btn, 2, 0);
      lv_obj_set_style_border_color(btn, types[i].color, 0);
      lv_obj_set_user_data(btn, (void *)(uintptr_t)types[i].type);
      lv_obj_add_event_cb(btn, type_select_cb, LV_EVENT_CLICKED, NULL);

      // Hover effect
      lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);

      // Icon
      lv_obj_t *icon = lv_label_create(btn);
      lv_label_set_text(icon, types[i].icon);
      lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
      lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 10);

      // Name
      lv_obj_t *name = lv_label_create(btn);
      lv_label_set_text(name, types[i].name);
      lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(name, lv_color_white(), 0);
      lv_obj_align(name, LV_ALIGN_CENTER, 0, 15);

      // Examples
      lv_obj_t *examples = lv_label_create(btn);
      lv_label_set_text(examples, types[i].examples);
      lv_obj_set_style_text_font(examples, &lv_font_montserrat_10, 0);
      lv_obj_set_style_text_color(examples, lv_color_hex(0xCCCCCC), 0);
      lv_label_set_long_mode(examples, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(examples, 190);
      lv_obj_set_style_text_align(examples, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(examples, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
  }

  lv_obj_clear_flag(popup_type_select, LV_OBJ_FLAG_HIDDEN);
}

// ====================================================================================
// UPDATE FUNCTIONS
// ====================================================================================

void ui_climate_update_dashboard(void) {
  if (!dashboard_container)
    return;

  // Clear existing cards
  lv_obj_clean(dashboard_container);
  memset(terrarium_cards, 0, sizeof(terrarium_cards));

  // Recreate cards
  uint8_t count = climate_get_terrarium_count();
  for (uint8_t i = 0; i < count; i++) {
    terrarium_config_t *t = climate_get_terrarium(i);
    if (t && t->active) {
      terrarium_cards[i] =
          ui_climate_create_terrarium_card(dashboard_container, t);
    }
  }

  // Add new terrarium button
  lv_obj_t *add_btn = lv_btn_create(dashboard_container);
  lv_obj_set_size(add_btn, 220, 180);
  lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_border_width(add_btn, 2, 0);
  lv_obj_set_style_border_color(add_btn, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_set_style_border_opa(add_btn, LV_OPA_30, 0);
  lv_obj_set_style_radius(add_btn, 16, 0);

  lv_obj_t *add_icon = lv_label_create(add_btn);
  lv_label_set_text(add_icon, LV_SYMBOL_PLUS);
  lv_obj_set_style_text_font(add_icon, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(add_icon, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_align(add_icon, LV_ALIGN_CENTER, 0, -15);

  lv_obj_t *add_label = lv_label_create(add_btn);
  lv_label_set_text(add_label, "Ajouter Terrarium");
  lv_obj_set_style_text_color(add_label, lv_color_hex(0x808080), 0);
  lv_obj_align(add_label, LV_ALIGN_CENTER, 0, 30);

  lv_obj_set_user_data(add_btn, (void *)(uintptr_t)TERRARIUM_DESERT);
  lv_obj_add_event_cb(add_btn, add_terrarium_cb, LV_EVENT_CLICKED, NULL);
}

void ui_climate_update_terrarium_detail(uint8_t terrarium_id) {
  terrarium_config_t *t = climate_get_terrarium(terrarium_id);
  if (!t)
    return;

  current_terrarium_id = terrarium_id;

  // Update temperature widgets
  if (detail_temp_hot_widget) {
    lv_obj_t *arc = lv_obj_get_child(detail_temp_hot_widget, 0);
    lv_obj_t *value = lv_obj_get_child(detail_temp_hot_widget, 1);
    if (arc && value) {
      lv_arc_set_value(arc, (int32_t)t->sensors.temp_hot_zone);
      char str[16];
      snprintf(str, sizeof(str), "%.1f°C", t->sensors.temp_hot_zone);
      lv_label_set_text(value, str);

      lv_color_t color = get_temp_color(
          t->sensors.temp_hot_zone, t->temp_day_hot_min, t->temp_day_hot_max);
      lv_obj_set_style_text_color(value, color, 0);
    }
  }

  if (detail_temp_cold_widget) {
    lv_obj_t *arc = lv_obj_get_child(detail_temp_cold_widget, 0);
    lv_obj_t *value = lv_obj_get_child(detail_temp_cold_widget, 1);
    if (arc && value) {
      lv_arc_set_value(arc, (int32_t)t->sensors.temp_cold_zone);
      char str[16];
      snprintf(str, sizeof(str), "%.1f°C", t->sensors.temp_cold_zone);
      lv_label_set_text(value, str);

      lv_color_t color =
          get_temp_color(t->sensors.temp_cold_zone, t->temp_day_cold_min,
                         t->temp_day_cold_max);
      lv_obj_set_style_text_color(value, color, 0);
    }
  }

  // Update humidity
  if (detail_humidity_widget) {
    lv_obj_t *bar = lv_obj_get_child(detail_humidity_widget, 0);
    lv_obj_t *value = lv_obj_get_child(detail_humidity_widget, 1);
    if (bar && value) {
      lv_bar_set_value(bar, (int32_t)t->sensors.humidity, LV_ANIM_ON);
      char str[16];
      snprintf(str, sizeof(str), "%.0f%%", t->sensors.humidity);
      lv_label_set_text(value, str);
    }
  }

  // Update water levels
  if (detail_basin_widget) {
    lv_obj_t *bar = lv_obj_get_child(detail_basin_widget, 0);
    lv_obj_t *value = lv_obj_get_child(detail_basin_widget, 1);
    if (bar && value) {
      lv_bar_set_value(bar, t->sensors.water_basin_level, LV_ANIM_ON);
      char str[16];
      snprintf(str, sizeof(str), "%d%%", t->sensors.water_basin_level);
      lv_label_set_text(value, str);

      lv_color_t color = (t->sensors.water_basin_level < t->water_basin_alert)
                             ? COLOR_ALERT_WARNING
                             : lv_color_hex(0x2196F3);
      lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    }
  }

  if (detail_reservoir_widget) {
    lv_obj_t *bar = lv_obj_get_child(detail_reservoir_widget, 0);
    lv_obj_t *value = lv_obj_get_child(detail_reservoir_widget, 1);
    if (bar && value) {
      lv_bar_set_value(bar, t->sensors.water_reservoir_level, LV_ANIM_ON);
      char str[16];
      snprintf(str, sizeof(str), "%d%%", t->sensors.water_reservoir_level);
      lv_label_set_text(value, str);

      lv_color_t color =
          (t->sensors.water_reservoir_level < t->water_reservoir_alert)
              ? COLOR_ALERT_WARNING
              : lv_color_hex(0x2196F3);
      lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    }
  }

  // Update equipment buttons
  if (detail_equipment_container) {
    for (int i = 0; i < lv_obj_get_child_count(detail_equipment_container);
         i++) {
      lv_obj_t *btn = lv_obj_get_child(detail_equipment_container, i);
      const char *name = (const char *)lv_obj_get_user_data(btn);
      if (!name)
        continue;

      bool is_on = false;
      bool has_error = false;

      if (strcmp(name, "heating") == 0) {
        is_on = t->equipment.heating_on;
        has_error = t->equipment.heating_error;
      } else if (strcmp(name, "uv") == 0) {
        is_on = t->equipment.uv_lamp_on;
        has_error = t->equipment.uv_lamp_error;
      } else if (strcmp(name, "light") == 0) {
        is_on = t->equipment.day_light_on;
      } else if (strcmp(name, "misting") == 0) {
        is_on = t->equipment.misting_on;
        has_error = t->equipment.misting_error;
      } else if (strcmp(name, "pump") == 0) {
        is_on = t->equipment.pump_on;
        has_error = t->equipment.pump_error;
      }

      if (has_error) {
        lv_obj_set_style_bg_color(btn, COLOR_EQUIP_ERROR, 0);
      } else {
        lv_obj_set_style_bg_color(btn, is_on ? COLOR_EQUIP_ON : COLOR_EQUIP_OFF,
                                  0);
      }
    }
  }
}

// ====================================================================================
// NAVIGATION
// ====================================================================================

void ui_climate_show_dashboard(void) {
  ESP_LOGI(TAG, "Showing climate dashboard");

  // SIMULATION DISABLED
  // climate_manager_start();
  // if (update_timer) { lv_timer_resume(update_timer); }

  if (page_climate_dashboard) {
    lv_obj_clear_flag(page_climate_dashboard, LV_OBJ_FLAG_HIDDEN);
  }
  if (page_terrarium_detail) {
    lv_obj_add_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);
  }
  ui_climate_update_dashboard();
}

void ui_climate_show_terrarium(uint8_t terrarium_id) {
  current_terrarium_id = terrarium_id;

  // SIMULATION DISABLED
  // climate_manager_start();
  // if (update_timer) { lv_timer_resume(update_timer); }

  if (page_climate_dashboard) {
    lv_obj_add_flag(page_climate_dashboard, LV_OBJ_FLAG_HIDDEN);
  }
  if (page_terrarium_detail) {
    lv_obj_clear_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);
  }

  ui_climate_update_terrarium_detail(terrarium_id);
}

// ====================================================================================
// TERRARIUM SETTINGS PAGE (Parameter Adjustment)
// ====================================================================================

static void humidity_slider_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  int32_t value = lv_slider_get_value(slider);

  terrarium_config_t *t = climate_get_terrarium(settings_terrarium_id);
  if (t) {
    t->humidity_max = (uint8_t)value;
    t->humidity_min = (uint8_t)(value > 10 ? value - 10 : 0);
  }

  if (settings_humidity_label) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", (int)value);
    lv_label_set_text(settings_humidity_label, buf);
  }
}

// Callback for misting mode switch
static void misting_switch_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

  terrarium_config_t *t = climate_get_terrarium(settings_terrarium_id);
  if (t) {
    t->misting.enabled = enabled;
    ESP_LOGI(TAG, "Misting mode changed to: %s", enabled ? "AUTO" : "MANUEL");
  }

  // Update the status label (passed as user_data)
  lv_obj_t *status_label = (lv_obj_t *)lv_event_get_user_data(e);
  if (status_label) {
    lv_label_set_text(status_label, enabled ? "AUTO" : "MANUEL");
  }
}

// Callback for light switch
static void light_switch_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

  terrarium_config_t *t = climate_get_terrarium(settings_terrarium_id);
  if (t) {
    t->light_schedule.enabled = enabled;
    ESP_LOGI(TAG, "Light schedule changed to: %s", enabled ? "ON" : "OFF");
  }
}

// Callback for reptile button - navigate to reptile detail in main.c
static void reptile_btn_cb(lv_event_t *e) {
  ESP_LOGI(TAG, "Reptile button clicked for terrarium %d",
           settings_terrarium_id);

  // Delete settings page and navigate to reptile detail
  if (page_terrarium_settings) {
    lv_obj_del(page_terrarium_settings);
    page_terrarium_settings = NULL;
  }

  // Call function in main.c to show reptile detail
  extern void show_reptile_for_terrarium(uint8_t terrarium_id);
  show_reptile_for_terrarium(settings_terrarium_id);
}

// === NUMERIC INPUT POPUP ===

// Close popup callback
static void temp_input_close_cb(lv_event_t *e) {
  (void)e;
  if (temp_input_popup) {
    lv_obj_del(temp_input_popup);
    temp_input_popup = NULL;
    temp_input_min_ta = NULL;
    temp_input_max_ta = NULL;
    temp_input_active_ta = NULL;
  }
}

// Confirm temperature input callback
static void temp_input_confirm_cb(lv_event_t *e) {
  (void)e;
  if (!temp_input_min_ta || !temp_input_max_ta)
    return;

  const char *min_text = lv_textarea_get_text(temp_input_min_ta);
  const char *max_text = lv_textarea_get_text(temp_input_max_ta);
  int min_value = atoi(min_text);
  int max_value = atoi(max_text);

  // Clamp values to allowed range
  if (min_value < temp_input_min)
    min_value = temp_input_min;
  if (max_value > temp_input_max)
    max_value = temp_input_max;
  if (min_value > max_value)
    min_value = max_value - 1;

  ESP_LOGI(TAG, "Temperature confirmed: min=%d, max=%d (zone %d)", min_value,
           max_value, temp_input_zone);

  // Update the label with min-max format
  if (temp_input_target_label) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d - %d°C", min_value, max_value);
    lv_label_set_text(temp_input_target_label, buf);
  }

  // Update terrarium config
  terrarium_config_t *t = climate_get_terrarium(settings_terrarium_id);
  if (t) {
    switch (temp_input_zone) {
    case 0: // Hot zone
      t->temp_day_hot_min = (float)min_value;
      t->temp_day_hot_max = (float)max_value;
      break;
    case 1: // Mid zone (not stored, just display)
      break;
    case 2: // Cold zone
      t->temp_day_cold_min = (float)min_value;
      t->temp_day_cold_max = (float)max_value;
      break;
    }
  }

  // Close popup
  temp_input_close_cb(NULL);
}

// Callback to switch keyboard focus between MIN and MAX textareas
static void temp_ta_focus_cb(lv_event_t *e) {
  lv_obj_t *ta = lv_event_get_target(e);
  lv_obj_t *popup = lv_obj_get_parent(lv_obj_get_parent(ta));
  // Find keyboard in popup children
  for (int i = 0; i < (int)lv_obj_get_child_count(popup); i++) {
    lv_obj_t *child = lv_obj_get_child(popup, i);
    if (lv_obj_check_type(child, &lv_keyboard_class)) {
      lv_keyboard_set_textarea(child, ta);
      temp_input_active_ta = ta;
      break;
    }
  }
}

// Show numeric input popup for temperature with MIN/MAX fields and NUMERIC
// KEYBOARD
static void show_temp_input_popup(const char *title, int current_val,
                                  int min_val, int max_val,
                                  lv_obj_t *target_label,
                                  lv_obj_t *target_slider, uint8_t zone) {
  // Close existing popup
  if (temp_input_popup) {
    lv_obj_del(temp_input_popup);
    temp_input_popup = NULL;
  }

  ESP_LOGI(TAG, "Creating temp popup with numeric keyboard for %s", title);

  // Store references
  temp_input_target_label = target_label;
  temp_input_target_slider = target_slider;
  temp_input_min = min_val;
  temp_input_max = max_val;
  temp_input_zone = zone;

  // Get current min/max from config
  terrarium_config_t *t = climate_get_terrarium(settings_terrarium_id);
  int cur_min = current_val - 2;
  int cur_max = current_val;
  if (t && zone == 0) {
    cur_min = (int)t->temp_day_hot_min;
    cur_max = (int)t->temp_day_hot_max;
  }
  if (t && zone == 2) {
    cur_min = (int)t->temp_day_cold_min;
    cur_max = (int)t->temp_day_cold_max;
  }

  // Create popup on the settings page (parent must be valid)
  if (!page_terrarium_settings) {
    ESP_LOGE(TAG, "Cannot create popup - settings page is NULL!");
    return;
  }
  temp_input_popup = lv_obj_create(page_terrarium_settings);
  lv_obj_set_size(temp_input_popup, 400, 420);
  lv_obj_align(temp_input_popup, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(temp_input_popup, COLOR_CLIMATE_BG_CARD, 0);
  lv_obj_set_style_radius(temp_input_popup, 12, 0);
  lv_obj_set_style_border_width(temp_input_popup, 2, 0);
  lv_obj_set_style_border_color(temp_input_popup, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_clear_flag(temp_input_popup, LV_OBJ_FLAG_SCROLLABLE);

  // Title
  lv_obj_t *title_lbl = lv_label_create(temp_input_popup);
  lv_label_set_text(title_lbl, title);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(title_lbl, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 8);

  // Row for MIN and MAX inputs
  lv_obj_t *input_row = lv_obj_create(temp_input_popup);
  lv_obj_set_size(input_row, 380, 60);
  lv_obj_align(input_row, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(input_row, 0, 0);
  lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

  // MIN label
  lv_obj_t *min_lbl = lv_label_create(input_row);
  lv_label_set_text(min_lbl, "MIN:");
  lv_obj_set_style_text_color(min_lbl, COLOR_TEMP_COLD, 0);
  lv_obj_set_style_text_font(min_lbl, &lv_font_montserrat_16, 0);
  lv_obj_align(min_lbl, LV_ALIGN_LEFT_MID, 10, 0);

  // MIN textarea (clickable - keyboard will attach)
  temp_input_min_ta = lv_textarea_create(input_row);
  lv_obj_set_size(temp_input_min_ta, 70, 45);
  lv_obj_align(temp_input_min_ta, LV_ALIGN_LEFT_MID, 60, 0);
  lv_textarea_set_max_length(temp_input_min_ta, 2);
  lv_textarea_set_one_line(temp_input_min_ta, true);
  char min_buf[8];
  snprintf(min_buf, sizeof(min_buf), "%d", cur_min);
  lv_textarea_set_text(temp_input_min_ta, min_buf);
  lv_obj_set_style_text_font(temp_input_min_ta, &lv_font_montserrat_22, 0);
  lv_obj_set_style_bg_color(temp_input_min_ta, lv_color_hex(0x2A2A4A), 0);
  lv_obj_set_style_text_color(temp_input_min_ta, COLOR_TEMP_COLD, 0);
  lv_obj_set_style_border_color(temp_input_min_ta, COLOR_TEMP_COLD, 0);
  lv_obj_set_style_border_width(temp_input_min_ta, 2, 0);
  lv_obj_set_style_radius(temp_input_min_ta, 8, 0);
  lv_obj_set_style_text_align(temp_input_min_ta, LV_TEXT_ALIGN_CENTER, 0);

  // MAX label
  lv_obj_t *max_lbl = lv_label_create(input_row);
  lv_label_set_text(max_lbl, "MAX:");
  lv_obj_set_style_text_color(max_lbl, COLOR_TEMP_HOT, 0);
  lv_obj_set_style_text_font(max_lbl, &lv_font_montserrat_16, 0);
  lv_obj_align(max_lbl, LV_ALIGN_RIGHT_MID, -130, 0);

  // MAX textarea (clickable - keyboard will attach)
  temp_input_max_ta = lv_textarea_create(input_row);
  lv_obj_set_size(temp_input_max_ta, 70, 45);
  lv_obj_align(temp_input_max_ta, LV_ALIGN_RIGHT_MID, -50, 0);
  lv_textarea_set_max_length(temp_input_max_ta, 2);
  lv_textarea_set_one_line(temp_input_max_ta, true);
  char max_buf[8];
  snprintf(max_buf, sizeof(max_buf), "%d", cur_max);
  lv_textarea_set_text(temp_input_max_ta, max_buf);
  lv_obj_set_style_text_font(temp_input_max_ta, &lv_font_montserrat_22, 0);
  lv_obj_set_style_bg_color(temp_input_max_ta, lv_color_hex(0x4A2A2A), 0);
  lv_obj_set_style_text_color(temp_input_max_ta, COLOR_TEMP_HOT, 0);
  lv_obj_set_style_border_color(temp_input_max_ta, COLOR_TEMP_HOT, 0);
  lv_obj_set_style_border_width(temp_input_max_ta, 2, 0);
  lv_obj_set_style_radius(temp_input_max_ta, 8, 0);
  lv_obj_set_style_text_align(temp_input_max_ta, LV_TEXT_ALIGN_CENTER, 0);

  // Range hint
  lv_obj_t *hint = lv_label_create(temp_input_popup);
  lv_label_set_text_fmt(hint, "Plage autorisée: %d - %d°C", min_val, max_val);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 105);

  // Numeric Keyboard - same style as WiFi keyboard
  lv_obj_t *kb = lv_keyboard_create(temp_input_popup);
  lv_obj_set_size(kb, 380, 200);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -55);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(kb, temp_input_min_ta); // Start with MIN selected
  temp_input_active_ta = temp_input_min_ta;

  // Style the keyboard to match the app theme
  lv_obj_set_style_bg_color(kb, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(0x2D5A3D), LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);

  // Event to switch keyboard between MIN and MAX when textareas are clicked
  lv_obj_add_event_cb(temp_input_min_ta, temp_ta_focus_cb, LV_EVENT_FOCUSED,
                      NULL);
  lv_obj_add_event_cb(temp_input_max_ta, temp_ta_focus_cb, LV_EVENT_FOCUSED,
                      NULL);

  // Cancel button
  lv_obj_t *cancel_btn = lv_btn_create(temp_input_popup);
  lv_obj_set_size(cancel_btn, 120, 40);
  lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 20, -8);
  lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x555555), 0);
  lv_obj_set_style_radius(cancel_btn, 8, 0);
  lv_obj_add_event_cb(cancel_btn, temp_input_close_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
  lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE " Annuler");
  lv_obj_center(cancel_lbl);

  // OK button
  lv_obj_t *ok_btn = lv_btn_create(temp_input_popup);
  lv_obj_set_size(ok_btn, 120, 40);
  lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
  lv_obj_set_style_bg_color(ok_btn, COLOR_CLIMATE_ACCENT, 0);
  lv_obj_set_style_radius(ok_btn, 8, 0);
  lv_obj_add_event_cb(ok_btn, temp_input_confirm_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *ok_lbl = lv_label_create(ok_btn);
  lv_label_set_text(ok_lbl, LV_SYMBOL_OK " Valider");
  lv_obj_center(ok_lbl);

  ESP_LOGI(TAG, "Popup with numeric keyboard created");
}

// Callback when temperature label is clicked
static void temp_label_clicked_cb(lv_event_t *e) {
  lv_obj_t *label = lv_event_get_target(e);
  uint8_t zone = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

  terrarium_config_t *t = climate_get_terrarium(settings_terrarium_id);
  int current = 30;
  const char *title = "Température";
  int min_val = 18, max_val = 45;
  lv_obj_t *slider = NULL;

  switch (zone) {
  case 0: // Hot
    current = t ? (int)t->temp_day_hot_max : 35;
    title = "Zone Chaude";
    min_val = 28;
    max_val = 45;
    slider = settings_temp_hot_slider;
    break;
  case 1: // Mid
    current = t ? (int)((t->temp_day_hot_max + t->temp_day_cold_max) / 2) : 30;
    title = "Zone Intermédiaire";
    min_val = 22;
    max_val = 38;
    break;
  case 2: // Cold
    current = t ? (int)t->temp_day_cold_max : 26;
    title = "Zone Froide";
    min_val = 18;
    max_val = 28;
    slider = settings_temp_cold_slider;
    break;
  }

  ESP_LOGI(TAG, "Temp label clicked: zone %d, current %d", zone, current);
  show_temp_input_popup(title, current, min_val, max_val, label, slider, zone);
}

void ui_climate_show_settings(uint8_t terrarium_id) {

  settings_terrarium_id = terrarium_id;
  terrarium_config_t *t = climate_get_terrarium(terrarium_id);

  ESP_LOGI(TAG, "Showing settings for terrarium %d", terrarium_id);

  // Hide dashboard
  if (page_climate_dashboard) {
    lv_obj_add_flag(page_climate_dashboard, LV_OBJ_FLAG_HIDDEN);
  }
  if (page_terrarium_detail) {
    lv_obj_add_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);
  }

  // Delete existing settings page if any
  if (page_terrarium_settings) {
    lv_obj_del(page_terrarium_settings);
    page_terrarium_settings = NULL;
  }

  // Create settings page - size adjusted to not cover status bar (50px) and
  // navbar (60px)
  page_terrarium_settings = lv_obj_create(ui_parent);
  lv_obj_set_size(page_terrarium_settings, 1024,
                  490); // 600 - 50 (status) - 60 (navbar)
  lv_obj_set_pos(page_terrarium_settings, 0, 50);
  lv_obj_set_style_bg_color(page_terrarium_settings, COLOR_CLIMATE_BG_DARK, 0);
  lv_obj_set_style_border_width(page_terrarium_settings, 0, 0);
  lv_obj_set_style_pad_all(page_terrarium_settings, 10, 0);
  lv_obj_set_style_pad_gap(page_terrarium_settings, 6, 0);
  lv_obj_set_flex_flow(page_terrarium_settings, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_terrarium_settings, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(page_terrarium_settings, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(page_terrarium_settings, LV_SCROLLBAR_MODE_AUTO);

  ESP_LOGI(TAG, "DEBUG: Page container created");

  // Header with back button and title
  lv_obj_t *header = lv_obj_create(page_terrarium_settings);
  lv_obj_set_size(header, LV_PCT(100), 40);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  // Back button
  lv_obj_t *back_btn = lv_btn_create(header);
  lv_obj_set_size(back_btn, 100, 40);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_CLIMATE_ACCENT, 0);
  lv_obj_set_style_radius(back_btn, 8, 0);
  lv_obj_add_event_cb(back_btn, back_from_settings_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_label);

  // Title
  lv_obj_t *title = lv_label_create(header);
  char title_buf[64];
  snprintf(title_buf, sizeof(title_buf), LV_SYMBOL_SETTINGS " Paramètres: %s",
           t ? t->name : "Terrarium");
  lv_label_set_text(title, title_buf);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 50, 0);

  ESP_LOGI(TAG, "DEBUG: Header created");

  // === TEMPERATURE SECTION (SIMPLIFIED - NO DROPDOWNS TO AVOID MEMORY ISSUES)
  // ===
  lv_obj_t *temp_section = lv_obj_create(page_terrarium_settings);
  lv_obj_set_size(temp_section, LV_PCT(100), 280);
  lv_obj_set_style_bg_color(temp_section, COLOR_CLIMATE_BG_CARD, 0);
  lv_obj_set_style_radius(temp_section, 12, 0);
  lv_obj_set_style_border_width(temp_section, 0, 0);
  lv_obj_set_style_pad_all(temp_section, 10, 0);
  lv_obj_set_flex_flow(temp_section, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(temp_section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(temp_section, 6, 0);

  // Title
  lv_obj_t *temp_title = lv_label_create(temp_section);
  lv_label_set_text(temp_title, LV_SYMBOL_CHARGE " Zones de Température");
  lv_obj_set_style_text_font(temp_title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(temp_title, COLOR_TEMP_HOT, 0);

  // === Zone Chaude (Hot) ===
  lv_obj_t *hot_row = lv_obj_create(temp_section);
  lv_obj_set_size(hot_row, LV_PCT(100), 70);
  lv_obj_set_style_bg_color(hot_row, lv_color_hex(0x3A2020), 0);
  lv_obj_set_style_radius(hot_row, 10, 0);
  lv_obj_set_style_border_width(hot_row, 1, 0);
  lv_obj_set_style_border_color(hot_row, COLOR_TEMP_HOT, 0);
  lv_obj_set_style_pad_all(hot_row, 8, 0);
  lv_obj_clear_flag(hot_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *hot_name = lv_label_create(hot_row);
  lv_label_set_text(hot_name, "Zone Chaude");
  lv_obj_set_style_text_color(hot_name, COLOR_TEMP_HOT, 0);
  lv_obj_set_style_text_font(hot_name, &lv_font_montserrat_14, 0);
  lv_obj_align(hot_name, LV_ALIGN_TOP_LEFT, 0, 0);

  settings_temp_hot_label = lv_label_create(hot_row);
  char hot_buf[24];
  int hot_min = t ? (int)t->temp_day_hot_min : 33;
  int hot_max = t ? (int)t->temp_day_hot_max : 35;
  snprintf(hot_buf, sizeof(hot_buf), "%d - %d°C", hot_min, hot_max);
  lv_label_set_text(settings_temp_hot_label, hot_buf);
  lv_obj_set_style_text_font(settings_temp_hot_label, &lv_font_montserrat_20,
                             0);
  lv_obj_set_style_text_color(settings_temp_hot_label, COLOR_TEMP_HOT, 0);
  lv_obj_align(settings_temp_hot_label, LV_ALIGN_TOP_LEFT, 120, 0);
  lv_obj_add_flag(settings_temp_hot_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(settings_temp_hot_label, temp_label_clicked_cb,
                      LV_EVENT_CLICKED, (void *)(uintptr_t)0);

  // Hot zone info (static labels instead of dropdowns)
  lv_obj_t *hot_info = lv_label_create(hot_row);
  lv_label_set_text(hot_info, "Tapis chauffant | Auto | ON");
  lv_obj_set_style_text_color(hot_info, lv_color_hex(0x888888), 0);
  lv_obj_align(hot_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // === Zone Intermédiaire (Mid) ===
  lv_obj_t *mid_row = lv_obj_create(temp_section);
  lv_obj_set_size(mid_row, LV_PCT(100), 70);
  lv_obj_set_style_bg_color(mid_row, lv_color_hex(0x3A3020), 0);
  lv_obj_set_style_radius(mid_row, 10, 0);
  lv_obj_set_style_border_width(mid_row, 1, 0);
  lv_obj_set_style_border_color(mid_row, lv_color_hex(0xFFA000), 0);
  lv_obj_set_style_pad_all(mid_row, 8, 0);
  lv_obj_clear_flag(mid_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *mid_name = lv_label_create(mid_row);
  lv_label_set_text(mid_name, "Zone Inter.");
  lv_obj_set_style_text_color(mid_name, lv_color_hex(0xFFA000), 0);
  lv_obj_set_style_text_font(mid_name, &lv_font_montserrat_14, 0);
  lv_obj_align(mid_name, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *mid_val = lv_label_create(mid_row);
  char mid_buf[24];
  int mid_min =
      t ? (int)((t->temp_day_hot_min + t->temp_day_cold_min) / 2) : 26;
  int mid_max =
      t ? (int)((t->temp_day_hot_max + t->temp_day_cold_max) / 2) : 30;
  snprintf(mid_buf, sizeof(mid_buf), "%d - %d°C", mid_min, mid_max);
  lv_label_set_text(mid_val, mid_buf);
  lv_obj_set_style_text_font(mid_val, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(mid_val, lv_color_hex(0xFFA000), 0);
  lv_obj_align(mid_val, LV_ALIGN_TOP_LEFT, 120, 0);
  lv_obj_add_flag(mid_val, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(mid_val, temp_label_clicked_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)1);

  lv_obj_t *mid_info = lv_label_create(mid_row);
  lv_label_set_text(mid_info, "Aucun chauffage");
  lv_obj_set_style_text_color(mid_info, lv_color_hex(0x888888), 0);
  lv_obj_align(mid_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // === Zone Froide (Cold) ===
  lv_obj_t *cold_row = lv_obj_create(temp_section);
  lv_obj_set_size(cold_row, LV_PCT(100), 70);
  lv_obj_set_style_bg_color(cold_row, lv_color_hex(0x203A3A), 0);
  lv_obj_set_style_radius(cold_row, 10, 0);
  lv_obj_set_style_border_width(cold_row, 1, 0);
  lv_obj_set_style_border_color(cold_row, COLOR_TEMP_COLD, 0);
  lv_obj_set_style_pad_all(cold_row, 8, 0);
  lv_obj_clear_flag(cold_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cold_name = lv_label_create(cold_row);
  lv_label_set_text(cold_name, "Zone Froide");
  lv_obj_set_style_text_color(cold_name, COLOR_TEMP_COLD, 0);
  lv_obj_set_style_text_font(cold_name, &lv_font_montserrat_14, 0);
  lv_obj_align(cold_name, LV_ALIGN_TOP_LEFT, 0, 0);

  settings_temp_cold_label = lv_label_create(cold_row);
  char cold_buf[24];
  int cold_min = t ? (int)t->temp_day_cold_min : 24;
  int cold_max = t ? (int)t->temp_day_cold_max : 26;
  snprintf(cold_buf, sizeof(cold_buf), "%d - %d°C", cold_min, cold_max);
  lv_label_set_text(settings_temp_cold_label, cold_buf);
  lv_obj_set_style_text_font(settings_temp_cold_label, &lv_font_montserrat_20,
                             0);
  lv_obj_set_style_text_color(settings_temp_cold_label, COLOR_TEMP_COLD, 0);
  lv_obj_align(settings_temp_cold_label, LV_ALIGN_TOP_LEFT, 120, 0);
  lv_obj_add_flag(settings_temp_cold_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(settings_temp_cold_label, temp_label_clicked_cb,
                      LV_EVENT_CLICKED, (void *)(uintptr_t)2);

  lv_obj_t *cold_info = lv_label_create(cold_row);
  lv_label_set_text(cold_info, "Aucun chauffage");
  lv_obj_set_style_text_color(cold_info, lv_color_hex(0x888888), 0);
  lv_obj_align(cold_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  ESP_LOGI(TAG, "DEBUG: Temperature section created (simplified)");

  // === HUMIDITY SECTION ===

  lv_obj_t *humid_section = lv_obj_create(page_terrarium_settings);
  lv_obj_set_size(humid_section, LV_PCT(100), 90);
  lv_obj_set_style_bg_color(humid_section, COLOR_CLIMATE_BG_CARD, 0);
  lv_obj_set_style_radius(humid_section, 16, 0);
  lv_obj_set_style_border_width(humid_section, 0, 0);
  lv_obj_set_style_pad_all(humid_section, 15, 0);

  lv_obj_t *humid_title = lv_label_create(humid_section);
  lv_label_set_text(humid_title, LV_SYMBOL_REFRESH " Consigne d'Humidité");
  lv_obj_set_style_text_font(humid_title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(humid_title, COLOR_HUMIDITY, 0);
  lv_obj_align(humid_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *humid_row = lv_obj_create(humid_section);
  lv_obj_set_size(humid_row, LV_PCT(100), 40);
  lv_obj_align(humid_row, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_obj_set_style_bg_opa(humid_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(humid_row, 0, 0);
  lv_obj_clear_flag(humid_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *humid_lbl = lv_label_create(humid_row);
  lv_label_set_text(humid_lbl, "Humidité cible:");
  lv_obj_set_style_text_color(humid_lbl, lv_color_white(), 0);
  lv_obj_align(humid_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  settings_humidity_slider = lv_slider_create(humid_row);
  lv_obj_set_size(settings_humidity_slider, 400, 20);
  lv_obj_align(settings_humidity_slider, LV_ALIGN_CENTER, 50, 0);
  lv_slider_set_range(settings_humidity_slider, 30, 90);
  lv_slider_set_value(settings_humidity_slider, t ? t->humidity_max : 60,
                      LV_ANIM_OFF);
  lv_obj_set_style_bg_color(settings_humidity_slider, lv_color_hex(0x333333),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(settings_humidity_slider, COLOR_HUMIDITY,
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(settings_humidity_slider, COLOR_HUMIDITY,
                            LV_PART_KNOB);
  lv_obj_add_event_cb(settings_humidity_slider, humidity_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  settings_humidity_label = lv_label_create(humid_row);
  char humid_buf[16];
  snprintf(humid_buf, sizeof(humid_buf), "%d%%", t ? t->humidity_max : 60);
  lv_label_set_text(settings_humidity_label, humid_buf);
  lv_obj_set_style_text_font(settings_humidity_label, &lv_font_montserrat_20,
                             0);
  lv_obj_set_style_text_color(settings_humidity_label, COLOR_HUMIDITY, 0);
  lv_obj_align(settings_humidity_label, LV_ALIGN_RIGHT_MID, 0, 0);

  // === BRUMISATION SECTION ===
  lv_obj_t *mist_section = lv_obj_create(page_terrarium_settings);
  lv_obj_set_size(mist_section, LV_PCT(100), 70);
  lv_obj_set_style_bg_color(mist_section, COLOR_CLIMATE_BG_CARD, 0);
  lv_obj_set_style_radius(mist_section, 12, 0);
  lv_obj_set_style_border_width(mist_section, 0, 0);
  lv_obj_set_style_pad_all(mist_section, 12, 0);

  lv_obj_t *mist_title = lv_label_create(mist_section);
  lv_label_set_text(mist_title, LV_SYMBOL_REFRESH " Brumisation");
  lv_obj_set_style_text_color(mist_title, COLOR_HUMIDITY, 0);
  lv_obj_set_style_text_font(mist_title, &lv_font_montserrat_14, 0);
  lv_obj_align(mist_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *mist_row = lv_obj_create(mist_section);
  lv_obj_set_size(mist_row, LV_PCT(100), 35);
  lv_obj_align(mist_row, LV_ALIGN_TOP_LEFT, 0, 25);
  lv_obj_set_style_bg_opa(mist_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(mist_row, 0, 0);
  lv_obj_clear_flag(mist_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *mist_mode_lbl = lv_label_create(mist_row);
  lv_label_set_text(mist_mode_lbl, "Mode:");
  lv_obj_set_style_text_color(mist_mode_lbl, lv_color_white(), 0);
  lv_obj_align(mist_mode_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *mist_sw = lv_switch_create(mist_row);
  lv_obj_set_size(mist_sw, 55, 28);
  lv_obj_align(mist_sw, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(mist_sw, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_set_style_bg_color(mist_sw, COLOR_HUMIDITY,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (t && t->misting.enabled)
    lv_obj_add_state(mist_sw, LV_STATE_CHECKED);

  lv_obj_t *mist_status = lv_label_create(mist_row);
  lv_label_set_text(mist_status, t && t->misting.enabled ? "AUTO" : "MANUEL");
  lv_obj_set_style_text_color(mist_status, COLOR_HUMIDITY, 0);
  lv_obj_set_style_text_font(mist_status, &lv_font_montserrat_16, 0);
  lv_obj_align(mist_status, LV_ALIGN_RIGHT_MID, 0, 0);

  // Add callback with status label as user_data
  lv_obj_add_event_cb(mist_sw, misting_switch_cb, LV_EVENT_VALUE_CHANGED,
                      mist_status);

  // === ECLAIRAGE SECTION ===
  lv_obj_t *light_section = lv_obj_create(page_terrarium_settings);
  lv_obj_set_size(light_section, LV_PCT(100), 100);
  lv_obj_set_style_bg_color(light_section, COLOR_CLIMATE_BG_CARD, 0);
  lv_obj_set_style_radius(light_section, 12, 0);
  lv_obj_set_style_border_width(light_section, 0, 0);
  lv_obj_set_style_pad_all(light_section, 12, 0);

  lv_obj_t *light_title = lv_label_create(light_section);
  lv_label_set_text(light_title, LV_SYMBOL_IMAGE " Éclairage");
  lv_obj_set_style_text_color(light_title, lv_color_hex(0xFFEB3B), 0);
  lv_obj_set_style_text_font(light_title, &lv_font_montserrat_14, 0);
  lv_obj_align(light_title, LV_ALIGN_TOP_LEFT, 0, 0);

  // Light on/off row
  lv_obj_t *light_row = lv_obj_create(light_section);
  lv_obj_set_size(light_row, LV_PCT(100), 30);
  lv_obj_align(light_row, LV_ALIGN_TOP_LEFT, 0, 22);
  lv_obj_set_style_bg_opa(light_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(light_row, 0, 0);
  lv_obj_clear_flag(light_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *light_lbl = lv_label_create(light_row);
  lv_label_set_text(light_lbl, "Lumière:");
  lv_obj_set_style_text_color(light_lbl, lv_color_white(), 0);
  lv_obj_align(light_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *light_sw = lv_switch_create(light_row);
  lv_obj_set_size(light_sw, 55, 28);
  lv_obj_align(light_sw, LV_ALIGN_CENTER, -50, 0);
  lv_obj_set_style_bg_color(light_sw, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_set_style_bg_color(light_sw, lv_color_hex(0xFFEB3B),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (t && t->light_schedule.enabled)
    lv_obj_add_state(light_sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(light_sw, light_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *light_times = lv_label_create(light_row);
  lv_label_set_text_fmt(
      light_times, "%02d:%02d - %02d:%02d", t ? t->light_schedule.on_hour : 7,
      t ? t->light_schedule.on_minute : 0, t ? t->light_schedule.off_hour : 19,
      t ? t->light_schedule.off_minute : 0);
  lv_obj_set_style_text_color(light_times, lv_color_hex(0xFFEB3B), 0);
  lv_obj_set_style_text_font(light_times, &lv_font_montserrat_16, 0);
  lv_obj_align(light_times, LV_ALIGN_RIGHT_MID, 0, 0);

  // UV Zone row
  lv_obj_t *uv_row = lv_obj_create(light_section);
  lv_obj_set_size(uv_row, LV_PCT(100), 30);
  lv_obj_align(uv_row, LV_ALIGN_TOP_LEFT, 0, 55);
  lv_obj_set_style_bg_opa(uv_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(uv_row, 0, 0);
  lv_obj_clear_flag(uv_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *uv_lbl = lv_label_create(uv_row);
  lv_label_set_text(uv_lbl, "Zone UV Ferguson:");
  lv_obj_set_style_text_color(uv_lbl, lv_color_white(), 0);
  lv_obj_align(uv_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *uv_val = lv_label_create(uv_row);
  lv_label_set_text_fmt(uv_val, "Zone %d", t ? t->uv_zone : 0);
  lv_obj_set_style_text_color(uv_val, COLOR_UV_ZONE_3, 0);
  lv_obj_set_style_text_font(uv_val, &lv_font_montserrat_16, 0);
  lv_obj_align(uv_val, LV_ALIGN_RIGHT_MID, 0, 0);

  // === INFO & REPTILE SECTION ===
  lv_obj_t *info_section = lv_obj_create(page_terrarium_settings);
  lv_obj_set_size(info_section, LV_PCT(100), 80);
  lv_obj_set_style_bg_color(info_section, lv_color_hex(0x1A2530), 0);
  lv_obj_set_style_radius(info_section, 12, 0);
  lv_obj_set_style_border_width(info_section, 1, 0);
  lv_obj_set_style_border_color(info_section, COLOR_CLIMATE_PRIMARY, 0);
  lv_obj_set_style_pad_all(info_section, 12, 0);

  // Type info
  lv_obj_t *type_lbl = lv_label_create(info_section);
  lv_label_set_text_fmt(type_lbl, "Type: %s   |   Nom: %s",
                        t ? climate_get_type_name(t->type) : "N/A",
                        t ? t->name : "N/A");
  lv_obj_set_style_text_color(type_lbl, lv_color_hex(0xB0B0B0), 0);
  lv_obj_align(type_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  // Reptile button
  lv_obj_t *reptile_btn = lv_btn_create(info_section);
  lv_obj_set_size(reptile_btn, 200, 35);
  lv_obj_align(reptile_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(reptile_btn, COLOR_CLIMATE_ACCENT, 0);
  lv_obj_set_style_bg_color(reptile_btn, lv_color_hex(0x006064),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(reptile_btn, 8, 0);

  lv_obj_t *reptile_lbl = lv_label_create(reptile_btn);
  lv_label_set_text(reptile_lbl, LV_SYMBOL_EYE_OPEN " Voir fiche reptile");
  lv_obj_center(reptile_lbl);
  lv_obj_add_event_cb(reptile_btn, reptile_btn_cb, LV_EVENT_CLICKED, NULL);

  ESP_LOGI(TAG, "Settings page created successfully for terrarium %d",
           terrarium_id);
}

void ui_climate_hide_all(void) {
  ESP_LOGI(TAG, "Hiding climate pages");

  // SIMULATION DISABLED
  // climate_manager_stop();
  // if (update_timer) { lv_timer_pause(update_timer); }

  if (page_climate_dashboard) {
    lv_obj_add_flag(page_climate_dashboard, LV_OBJ_FLAG_HIDDEN);
  }
  if (page_terrarium_detail) {
    lv_obj_add_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN);
  }
  if (page_schedule_detail) {
    lv_obj_add_flag(page_schedule_detail, LV_OBJ_FLAG_HIDDEN);
  }
  if (page_alerts_list) {
    lv_obj_add_flag(page_alerts_list, LV_OBJ_FLAG_HIDDEN);
  }
  if (popup_type_select) {
    lv_obj_add_flag(popup_type_select, LV_OBJ_FLAG_HIDDEN);
  }
  // Delete settings page completely to free memory and avoid conflicts
  if (page_terrarium_settings) {
    lv_obj_del(page_terrarium_settings);
    page_terrarium_settings = NULL;
  }
  ESP_LOGI(TAG, "Climate pages hidden/deleted");
}

uint8_t ui_climate_get_alert_count(void) {
  return climate_get_active_alert_count();
}

// ====================================================================================
// TIMER CALLBACK
// ====================================================================================

void ui_climate_timer_cb(lv_timer_t *timer) {
  (void)timer;

  // Only update visible page
  if (page_climate_dashboard &&
      !lv_obj_has_flag(page_climate_dashboard, LV_OBJ_FLAG_HIDDEN)) {
    // Refresh cards with new sensor data
    ui_climate_update_dashboard();
  }

  if (page_terrarium_detail &&
      !lv_obj_has_flag(page_terrarium_detail, LV_OBJ_FLAG_HIDDEN)) {
    ui_climate_update_terrarium_detail(current_terrarium_id);
  }
}

// ====================================================================================
// INITIALISATION
// ====================================================================================

void ui_climate_init(lv_obj_t *parent) {
  ui_parent = parent;

  ESP_LOGI(TAG, "Initializing Climate UI...");

  // Initialize styles
  init_styles();

  // Initialize climate manager - SIMULATION DISABLED
  climate_manager_init();

  // Create pages
  ui_climate_create_dashboard(parent);
  ui_climate_create_terrarium_detail(parent);

  // SIMULATION COMPLETELY DISABLED - causes UI issues
  // climate_manager_start();

  // Add some demo terrariums (static data only, no simulation)
  climate_add_terrarium(TERRARIUM_DESERT, "Terra Pogona");
  climate_add_terrarium(TERRARIUM_TROPICAL, "Terra Python Vert");
  climate_add_terrarium(TERRARIUM_SEMI_TROPICAL, "Terra Boa");

  // Update dashboard once (static)
  ui_climate_update_dashboard();

  // Hide climate pages by default
  ui_climate_hide_all();

  // TIMER DISABLED - was causing UI refresh conflicts
  // update_timer = lv_timer_create(ui_climate_timer_cb, 1000, NULL);
  // lv_timer_pause(update_timer);
  update_timer = NULL;

  ESP_LOGI(TAG, "Climate UI initialized (SIMULATION DISABLED)");
}
