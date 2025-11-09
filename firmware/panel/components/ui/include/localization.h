#pragma once

#include <stdint.h>
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_LANG_FR = 0,
    UI_LANG_EN,
    UI_LANG_ES,
    UI_LANG_COUNT,
} ui_language_t;

typedef enum {
    UI_STR_TAB_DASHBOARD = 0,
    UI_STR_TAB_CONTROL,
    UI_STR_TAB_SETTINGS,
    UI_STR_STATUS_CONNECTING,
    UI_STR_STATUS_LAST_UPDATE,
    UI_STR_STATUS_LANGUAGE_CHANGED,
    UI_STR_SENSOR_SECTION,
    UI_STR_SENSOR_SHT31,
    UI_STR_SENSOR_SHT21,
    UI_STR_SENSOR_BME280,
    UI_STR_SENSOR_DS18B20,
    UI_STR_SENSOR_AMBIENT,
    UI_STR_DOME_SECTION,
    UI_STR_DOME_ACTIVE,
    UI_STR_DOME_IDLE,
    UI_STR_INTERLOCK_OK,
    UI_STR_INTERLOCK_ACTIVE,
    UI_STR_ALARM_MUTE,
    UI_STR_ALARM_UNMUTE,
    UI_STR_TELEMETRY_SECTION,
    UI_STR_OTA_SECTION,
    UI_STR_OTA_CONTROLLER_PATH,
    UI_STR_OTA_CONTROLLER_UPLOAD,
    UI_STR_OTA_DOME_PATH,
    UI_STR_OTA_DOME_UPLOAD,
    UI_STR_OTA_NO_PATH,
    UI_STR_OTA_IN_PROGRESS,
    UI_STR_SPECIES_SECTION,
    UI_STR_SPECIES_APPLY,
    UI_STR_SPECIES_REFRESH,
    UI_STR_SPECIES_NO_SELECTION,
    UI_STR_SPECIES_APPLIED,
    UI_STR_LIGHT_CCT_DAY,
    UI_STR_LIGHT_CCT_WARM,
    UI_STR_LIGHT_UVA_SET,
    UI_STR_LIGHT_UVA_CLAMP,
    UI_STR_LIGHT_UVB_SET,
    UI_STR_LIGHT_UVB_CLAMP,
    UI_STR_LIGHT_UVB_PERIOD,
    UI_STR_LIGHT_UVB_DUTY,
    UI_STR_LIGHT_SKY,
    UI_STR_LIGHT_APPLY,
    UI_STR_LANGUAGE_SELECT,
    UI_STR_CALIB_SECTION,
    UI_STR_CALIB_K,
    UI_STR_CALIB_UVI_MAX,
    UI_STR_CALIB_FETCH,
    UI_STR_CALIB_APPLY,
    UI_STR_NETWORK_SECTION,
    UI_STR_NETWORK_SSID,
    UI_STR_NETWORK_PASSWORD,
    UI_STR_NETWORK_HOST,
    UI_STR_NETWORK_PORT,
    UI_STR_NETWORK_TLS,
    UI_STR_NETWORK_SAVE,
    UI_STR_NETWORK_SAVED,
    UI_STR_SENSOR_VALUE_TEMP_HUM,
    UI_STR_SENSOR_VALUE_HUM,
    UI_STR_SENSOR_VALUE_PRESSURE,
    UI_STR_SENSOR_VALUE_PRESSURE_HUM,
    UI_STR_SENSOR_VALUE_UVI,
    UI_STR_SENSOR_VALUE_HEATSINK,
    UI_STR_SENSOR_VALUE_UNKNOWN,
    UI_STR_CALIB_STATUS,
    UI_STR_ERROR_NETWORK,
    UI_STR_ERROR_LIGHT,
    UI_STR_ERROR_ALARM,
    UI_STR_ERROR_CALIBRATION,
    UI_STR_ERROR_SPECIES,
    UI_STR_ERROR_CONFIG,
    UI_STR_ERROR_OTA,
    UI_STR_COUNT
} ui_string_id_t;

const char *ui_loc_get(ui_language_t lang, ui_string_id_t id);
ui_language_t ui_loc_from_code(const char *code);
const char *ui_loc_to_code(ui_language_t lang);
uint16_t ui_loc_language_index(ui_language_t lang);
ui_language_t ui_loc_language_from_index(uint16_t idx);
const char *ui_loc_language_options(void);
const char *ui_loc_select_label(const terrarium_species_entry_t *entry, ui_language_t lang);
const terrarium_species_entry_t *ui_loc_find_species_by_label(const terrarium_species_catalog_t *catalog,
                                                              ui_language_t lang,
                                                              const char *label);
const char *ui_loc_label_for_key(const terrarium_species_catalog_t *catalog, ui_language_t lang, const char *key);
uint16_t ui_loc_index_for_label(const char *options, const char *label);

#ifdef __cplusplus
}
#endif

