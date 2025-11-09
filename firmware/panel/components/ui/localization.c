#include "localization.h"

#include <stdbool.h>
#include <string.h>

#define LANG_OPTION_STRING "Français\nEnglish\nEspañol"

typedef struct {
    const char *fr;
    const char *en;
    const char *es;
} ui_translation_t;

static const ui_translation_t s_translations[UI_STR_COUNT] = {
    [UI_STR_TAB_DASHBOARD] = {"Tableau de bord", "Dashboard", "Panel"},
    [UI_STR_TAB_CONTROL] = {"Contrôle", "Control", "Control"},
    [UI_STR_TAB_SETTINGS] = {"Paramètres", "Settings", "Ajustes"},
    [UI_STR_STATUS_CONNECTING] = {"Connexion en cours…", "Connecting…", "Conectando…"},
    [UI_STR_STATUS_LAST_UPDATE] = {"Dernière mise à jour: %llu ms", "Last update: %llu ms", "Última actualización: %llu ms"},
    [UI_STR_STATUS_LANGUAGE_CHANGED] = {"Langue enregistrée", "Language saved", "Idioma guardado"},
    [UI_STR_SENSOR_SECTION] = {"Capteurs", "Sensors", "Sensores"},
    [UI_STR_SENSOR_SHT31] = {"SHT31", "SHT31", "SHT31"},
    [UI_STR_SENSOR_SHT21] = {"SHT21", "SHT21", "SHT21"},
    [UI_STR_SENSOR_BME280] = {"BME280", "BME280", "BME280"},
    [UI_STR_SENSOR_DS18B20] = {"DS18B20", "DS18B20", "DS18B20"},
    [UI_STR_SENSOR_AMBIENT] = {"Ambiant", "Ambient", "Ambiente"},
    [UI_STR_DOME_SECTION] = {"État du dôme", "Dome status", "Estado del domo"},
    [UI_STR_DOME_ACTIVE] = {"Dôme actif", "Dome active", "Domo activo"},
    [UI_STR_DOME_IDLE] = {"Dôme inactif", "Dome idle", "Domo inactivo"},
    [UI_STR_INTERLOCK_OK] = {"Interlock: OK", "Interlock: OK", "Interlock: OK"},
    [UI_STR_INTERLOCK_ACTIVE] = {"Interlock: actif", "Interlock: active", "Interlock: activo"},
    [UI_STR_ALARM_MUTE] = {"Couper alarmes", "Mute alarms", "Silenciar alarmas"},
    [UI_STR_ALARM_UNMUTE] = {"Activer alarmes", "Unmute alarms", "Activar alarmas"},
    [UI_STR_TELEMETRY_SECTION] = {"Télémétries", "Telemetry", "Telemetría"},
    [UI_STR_OTA_SECTION] = {"Mises à jour OTA", "OTA updates", "Actualizaciones OTA"},
    [UI_STR_OTA_CONTROLLER_PATH] = {"Fichier contrôleur", "Controller file", "Archivo controlador"},
    [UI_STR_OTA_CONTROLLER_UPLOAD] = {"Flasher contrôleur", "Upload controller", "Flashear controlador"},
    [UI_STR_OTA_DOME_PATH] = {"Fichier dôme", "Dome file", "Archivo domo"},
    [UI_STR_OTA_DOME_UPLOAD] = {"Flasher dôme", "Upload dome", "Flashear domo"},
    [UI_STR_OTA_NO_PATH] = {"Chemin OTA manquant", "Missing OTA path", "Ruta OTA faltante"},
    [UI_STR_OTA_IN_PROGRESS] = {"Téléversement OTA…", "Uploading OTA…", "Subiendo OTA…"},
    [UI_STR_SPECIES_SECTION] = {"Profils d'espèces", "Species profiles", "Perfiles de especie"},
    [UI_STR_SPECIES_APPLY] = {"Appliquer", "Apply", "Aplicar"},
    [UI_STR_SPECIES_REFRESH] = {"Rafraîchir", "Refresh", "Actualizar"},
    [UI_STR_SPECIES_NO_SELECTION] = {"Sélectionnez un profil", "Select a profile", "Seleccione un perfil"},
    [UI_STR_SPECIES_APPLIED] = {"Profil appliqué", "Profile applied", "Perfil aplicado"},
    [UI_STR_LIGHT_CCT_DAY] = {"CCT jour (‰)", "CCT day (‰)", "CCT día (‰)"},
    [UI_STR_LIGHT_CCT_WARM] = {"CCT chaud (‰)", "CCT warm (‰)", "CCT cálido (‰)"},
    [UI_STR_LIGHT_UVA_SET] = {"UVA consigne (‰)", "UVA setpoint (‰)", "UVA consigna (‰)"},
    [UI_STR_LIGHT_UVA_CLAMP] = {"UVA limite (‰)", "UVA clamp (‰)", "UVA límite (‰)"},
    [UI_STR_LIGHT_UVB_SET] = {"UVB consigne (‰)", "UVB setpoint (‰)", "UVB consigna (‰)"},
    [UI_STR_LIGHT_UVB_CLAMP] = {"UVB limite (‰)", "UVB clamp (‰)", "UVB límite (‰)"},
    [UI_STR_LIGHT_UVB_PERIOD] = {"Période UVB (s)", "UVB period (s)", "Periodo UVB (s)"},
    [UI_STR_LIGHT_UVB_DUTY] = {"Duty UVB (‰)", "UVB duty (‰)", "Duty UVB (‰)"},
    [UI_STR_LIGHT_SKY] = {"Mode ciel", "Sky mode", "Modo cielo"},
    [UI_STR_LIGHT_APPLY] = {"Appliquer", "Apply", "Aplicar"},
    [UI_STR_LANGUAGE_SELECT] = {"Langue", "Language", "Idioma"},
    [UI_STR_CALIB_SECTION] = {"Calibration UVB", "UVB calibration", "Calibración UVB"},
    [UI_STR_CALIB_K] = {"Pente k", "Slope k", "Pendiente k"},
    [UI_STR_CALIB_UVI_MAX] = {"UVI max", "UVI max", "UVI máx"},
    [UI_STR_CALIB_FETCH] = {"Lire", "Fetch", "Leer"},
    [UI_STR_CALIB_APPLY] = {"Appliquer", "Apply", "Aplicar"},
    [UI_STR_NETWORK_SECTION] = {"Réseau", "Network", "Red"},
    [UI_STR_NETWORK_SSID] = {"SSID", "SSID", "SSID"},
    [UI_STR_NETWORK_PASSWORD] = {"Mot de passe", "Password", "Contraseña"},
    [UI_STR_NETWORK_HOST] = {"Hôte contrôleur", "Controller host", "Host controlador"},
    [UI_STR_NETWORK_PORT] = {"Port", "Port", "Puerto"},
    [UI_STR_NETWORK_TLS] = {"Utiliser HTTPS", "Use HTTPS", "Usar HTTPS"},
    [UI_STR_NETWORK_SAVE] = {"Sauvegarder", "Save", "Guardar"},
    [UI_STR_NETWORK_SAVED] = {"Paramètres réseau sauvegardés", "Network settings saved", "Parámetros de red guardados"},
    [UI_STR_SENSOR_VALUE_TEMP_HUM] = {"Température : %.1f °C / Humidité : %.1f %%", "Temperature: %.1f °C / Humidity: %.1f %%", "Temperatura: %.1f °C / Humedad: %.1f %%"},
    [UI_STR_SENSOR_VALUE_HUM] = {"Humidité : %.1f %%", "Humidity: %.1f %%", "Humedad: %.1f %%"},
    [UI_STR_SENSOR_VALUE_PRESSURE] = {"Pression : %.1f hPa", "Pressure: %.1f hPa", "Presión: %.1f hPa"},
    [UI_STR_SENSOR_VALUE_PRESSURE_HUM] = {"Pression : %.1f hPa / Humidité : %.1f %%", "Pressure: %.1f hPa / Humidity: %.1f %%", "Presión: %.1f hPa / Humedad: %.1f %%"},
    [UI_STR_SENSOR_VALUE_UVI] = {"UVI : %.1f", "UVI: %.1f", "UVI: %.1f"},
    [UI_STR_SENSOR_VALUE_HEATSINK] = {"Dissipateur : %.1f °C", "Heatsink: %.1f °C", "Disipador: %.1f °C"},
    [UI_STR_SENSOR_VALUE_UNKNOWN] = {"Indisponible", "Unavailable", "No disponible"},
    [UI_STR_CALIB_STATUS] = {"k = %.2f / UVI = %.2f", "k = %.2f / UVI = %.2f", "k = %.2f / UVI = %.2f"},
    [UI_STR_ERROR_NETWORK] = {"Erreur réseau", "Network error", "Error de red"},
    [UI_STR_ERROR_LIGHT] = {"Erreur éclairage", "Lighting error", "Error de iluminación"},
    [UI_STR_ERROR_ALARM] = {"Erreur alarmes", "Alarm error", "Error de alarma"},
    [UI_STR_ERROR_CALIBRATION] = {"Erreur calibration", "Calibration error", "Error de calibración"},
    [UI_STR_ERROR_SPECIES] = {"Erreur profils", "Species error", "Error de perfiles"},
    [UI_STR_ERROR_CONFIG] = {"Erreur configuration", "Config error", "Error de configuración"},
    [UI_STR_ERROR_OTA] = {"Erreur OTA", "OTA error", "Error OTA"},
};

static const char *s_language_codes[UI_LANG_COUNT] = {"fr", "en", "es"};

const char *ui_loc_get(ui_language_t lang, ui_string_id_t id)
{
    if (id >= UI_STR_COUNT) {
        return "";
    }
    if (lang >= UI_LANG_COUNT) {
        lang = UI_LANG_FR;
    }
    const ui_translation_t *t = &s_translations[id];
    switch (lang) {
    case UI_LANG_EN: return t->en ? t->en : t->fr;
    case UI_LANG_ES: return t->es ? t->es : (t->en ? t->en : t->fr);
    case UI_LANG_FR:
    default: return t->fr ? t->fr : t->en;
    }
}

ui_language_t ui_loc_from_code(const char *code)
{
    if (!code) {
        return UI_LANG_FR;
    }
    for (uint16_t i = 0; i < UI_LANG_COUNT; ++i) {
        if (strcmp(code, s_language_codes[i]) == 0) {
            return (ui_language_t)i;
        }
    }
    return UI_LANG_FR;
}

const char *ui_loc_to_code(ui_language_t lang)
{
    if (lang >= UI_LANG_COUNT) {
        lang = UI_LANG_FR;
    }
    return s_language_codes[lang];
}

uint16_t ui_loc_language_index(ui_language_t lang)
{
    if (lang >= UI_LANG_COUNT) {
        return 0;
    }
    return (uint16_t)lang;
}

ui_language_t ui_loc_language_from_index(uint16_t idx)
{
    if (idx >= UI_LANG_COUNT) {
        return UI_LANG_FR;
    }
    return (ui_language_t)idx;
}

const char *ui_loc_language_options(void)
{
    return LANG_OPTION_STRING;
}

const char *ui_loc_select_label(const terrarium_species_entry_t *entry, ui_language_t lang)
{
    if (!entry) {
        return NULL;
    }
    switch (lang) {
    case UI_LANG_EN: return entry->label_en[0] ? entry->label_en : entry->label_fr;
    case UI_LANG_ES: return entry->label_es[0] ? entry->label_es : (entry->label_en[0] ? entry->label_en : entry->label_fr);
    case UI_LANG_FR:
    default: return entry->label_fr[0] ? entry->label_fr : entry->label_en;
    }
}

const terrarium_species_entry_t *ui_loc_find_species_by_label(const terrarium_species_catalog_t *catalog,
                                                              ui_language_t lang,
                                                              const char *label)
{
    if (!catalog || !label) {
        return NULL;
    }
    for (size_t i = 0; i < catalog->count; ++i) {
        const terrarium_species_entry_t *entry = &catalog->entries[i];
        const char *candidate = ui_loc_select_label(entry, lang);
        if (candidate && strcmp(candidate, label) == 0) {
            return entry;
        }
    }
    return NULL;
}

const char *ui_loc_label_for_key(const terrarium_species_catalog_t *catalog, ui_language_t lang, const char *key)
{
    if (!catalog || !key) {
        return NULL;
    }
    for (size_t i = 0; i < catalog->count; ++i) {
        const terrarium_species_entry_t *entry = &catalog->entries[i];
        if (strcmp(entry->key, key) == 0) {
            return ui_loc_select_label(entry, lang);
        }
    }
    return NULL;
}

uint16_t ui_loc_index_for_label(const char *options, const char *label)
{
    if (!options || !label) {
        return 0;
    }
    uint16_t index = 0;
    const char *start = options;
    while (*start) {
        const char *end = strchr(start, '\n');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (strncmp(start, label, len) == 0 && label[len] == '\0') {
            return index;
        }
        if (!end) {
            break;
        }
        start = end + 1;
        ++index;
    }
    return 0;
}

