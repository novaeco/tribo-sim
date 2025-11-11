#include "httpd.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_err.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#include "drivers/dome_bus.h"
#include "drivers/dome_i2c.h"
#include "drivers/sensors.h"
#include "drivers/calib.h"
#include "drivers/alarms.h"
#include "drivers/tca9548a.h"
#include "drivers/climate.h"
#include "include/config.h"
#include "include/dome_regs.h"
#include "net/credentials.h"
#include "net/light_payload.h"
#include "species_profiles.h"
#include "ota_stream.h"
#include "ota_manifest.h"
#include "ota_state.h"

static const char *TAG = "HTTPSD";

static httpd_handle_t s_server = NULL;

#define OTA_MANIFEST_HEADER "X-OTA-Manifest"
#define OTA_MANIFEST_MAX_HEADER_LEN 4096

static esp_err_t read_manifest_header(httpd_req_t *req, ota_manifest_t *manifest)
{
    if (!manifest) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t hdr_len = httpd_req_get_hdr_value_len(req, OTA_MANIFEST_HEADER);
    if (hdr_len == 0 || hdr_len > OTA_MANIFEST_MAX_HEADER_LEN) {
        ESP_LOGE(TAG, "Missing or oversized manifest header (%zu)", hdr_len);
        return ESP_ERR_INVALID_ARG;
    }
    char *b64 = calloc(1, hdr_len + 1);
    if (!b64) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = httpd_req_get_hdr_value_str(req, OTA_MANIFEST_HEADER, b64, hdr_len + 1);
    if (err != ESP_OK) {
        free(b64);
        ESP_LOGE(TAG, "Failed to read manifest header: %s", esp_err_to_name(err));
        return err;
    }
    size_t json_cap = (hdr_len * 3) / 4 + 4;
    uint8_t *json = calloc(1, json_cap);
    if (!json) {
        free(b64);
        return ESP_ERR_NO_MEM;
    }
    size_t json_len = 0;
    int rc = mbedtls_base64_decode(json, json_cap, &json_len, (const unsigned char *)b64, hdr_len);
    free(b64);
    if (rc != 0) {
        free(json);
        ESP_LOGE(TAG, "Manifest base64 decode failed (%d)", rc);
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t parse_err = ota_manifest_parse((const char *)json, json_len, manifest);
    free(json);
    if (parse_err != ESP_OK) {
        return parse_err;
    }
    return ota_manifest_verify(manifest);
}

static void put_u32_le(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
    out[2] = (uint8_t)((value >> 16) & 0xFF);
    out[3] = (uint8_t)((value >> 24) & 0xFF);
}

static esp_err_t dome_write_status_message(const char *msg)
{
    uint8_t buf[DOME_REG_OTA_STATUS_MSG_LEN] = {0};
    if (msg && msg[0]) {
        strncpy((char *)buf, msg, DOME_REG_OTA_STATUS_MSG_LEN - 1);
    }
    return dome_bus_write(DOME_REG_OTA_STATUS_MSG, buf, sizeof(buf));
}

static esp_err_t dome_stage_manifest(const ota_manifest_t *manifest, const char *message)
{
    uint8_t size_buf[4];
    put_u32_le(size_buf, manifest->image_size);
    ESP_RETURN_ON_ERROR(dome_bus_write(DOME_REG_OTA_EXPECTED_SIZE_L, size_buf, sizeof(size_buf)), TAG, "dome size");
    ESP_RETURN_ON_ERROR(dome_bus_write(DOME_REG_OTA_EXPECTED_SHA, manifest->image_sha256, 32), TAG, "dome sha");
    uint8_t version_buf[DOME_REG_OTA_VERSION_LEN] = {0};
    strncpy((char *)version_buf, manifest->version, DOME_REG_OTA_VERSION_LEN - 1);
    ESP_RETURN_ON_ERROR(dome_bus_write(DOME_REG_OTA_VERSION, version_buf, sizeof(version_buf)), TAG, "dome version");
    uint8_t flags = DOME_OTA_FLAG_META_READY;
    ESP_RETURN_ON_ERROR(dome_bus_write(DOME_REG_OTA_FLAGS, &flags, 1), TAG, "dome flags");
    return dome_write_status_message(message);
}

static esp_err_t send_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_status(req, HTTPD_401_UNAUTHORIZED);
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"Terrarium\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
    return ESP_ERR_HTTPD_RESP_SENT;
}

#define REQUIRE_AUTH_OR_RETURN(_req)            \
    do {                                       \
        esp_err_t __auth = httpd_require_auth(_req); \
        if (__auth != ESP_OK) {                 \
            return __auth;                     \
        }                                      \
    } while (0)

static esp_err_t httpd_require_auth(httpd_req_t *req)
{
    esp_err_t cred_err = credentials_init();
    if (cred_err != ESP_OK) {
        ESP_LOGE(TAG, "credentials_init failed: %s", esp_err_to_name(cred_err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "auth init failed");
        return ESP_ERR_HTTPD_RESP_SENT;
    }
    size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (auth_len == 0 || auth_len >= 128) {
        return send_unauthorized(req);
    }
    char authorization[128];
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", authorization, sizeof(authorization));
    if (err != ESP_OK) {
        return send_unauthorized(req);
    }
    if (!credentials_authorize_bearer(authorization)) {
        return send_unauthorized(req);
    }
    return ESP_OK;
}

static const char ROOT_HTML[] =
    "<!doctype html><html lang='en'><head><meta charset='utf-8'><title>Terrarium S3</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:system-ui,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:0;padding:24px;background:#101420;color:#f1f5ff}"
    "h1{margin-top:0;font-size:1.8rem}section{margin-bottom:32px;padding:16px;border-radius:16px;background:rgba(21,30,46,0.72);"
    "backdrop-filter:blur(8px);box-shadow:0 12px 40px rgba(10,10,20,0.4)}label{display:block;margin-bottom:6px;font-size:0.9rem}"
    "input,select,button{padding:10px;border-radius:10px;border:1px solid rgba(255,255,255,0.1);background:rgba(255,255,255,0.08);"
    "color:#fefefe;margin-bottom:12px;width:100%;box-sizing:border-box}button{cursor:pointer;font-weight:600;background:#3a86ff;}"
    "button.secondary{background:#6c757d;}#chartContainer{position:relative;height:260px;margin-top:16px;border-radius:12px;overflow:hidden;"
    "background:rgba(7,11,20,0.6);}canvas{width:100%;height:100%;}table{width:100%;border-collapse:collapse;}th,td{padding:6px 8px;"
    "border-bottom:1px solid rgba(255,255,255,0.08);}#statusBanner{padding:12px;border-radius:12px;margin-bottom:16px;font-weight:600;}"
    "#statusBanner.error{background:rgba(220,53,69,0.15);color:#ffb4c0;}#statusBanner.ok{background:rgba(40,167,69,0.18);color:#b7ffce;}"
    "progress{width:100%;height:16px;border-radius:12px;overflow:hidden;background:rgba(255,255,255,0.1);}progress::-webkit-progress-bar{background:transparent;}"
    "progress::-webkit-progress-value{background:#3a86ff;}details{margin-top:12px;}summary{cursor:pointer;font-weight:600;}#speciesMetadata strong{display:block;font-size:0.8rem;color:rgba(255,255,255,0.72);}#speciesMetadata span{display:block;margin-top:2px;font-weight:600;}#speciesMetadata h3{margin:0 0 8px;font-size:1rem;}"
    ".ota-block{margin-top:12px;padding:12px;border-radius:12px;background:rgba(0,0,0,0.18);border:1px solid rgba(255,255,255,0.08);}""
    " .ota-block h3{margin:0 0 8px;font-size:1.1rem;} .ota-status-line{font-size:0.85rem;margin-top:6px;color:rgba(255,255,255,0.8);}""
    " .ota-status-line span{display:block;margin-top:2px;word-break:break-all;}""</style></head><body>"
    "<h1>Terrarium S3</h1>"
    "<div id='statusBanner' class='ok'></div>"
    "<section><label for='languageSelect' data-i18n='language'></label><select id='languageSelect'></select>"
    "<label for='speciesSelect' data-i18n='species_profile'></label><select id='speciesSelect'></select><button id='applySpecies' data-i18n='apply_profile'></button>"
    "<div id='speciesMetadata' style='display:none;margin-top:12px;padding:12px;border-radius:12px;background:rgba(255,255,255,0.05);'>"
    "<h3 data-i18n='profile_details'></h3>"
    "<div><strong data-i18n='profile_common_name'></strong><span id='metaName'></span></div>"
    "<div><strong data-i18n='profile_type'></strong><span id='metaType'></span></div>"
    "<div><strong data-i18n='metadata_habitat'></strong><span id='metaHabitat'></span></div>"
    "<div><strong data-i18n='metadata_uv_category'></strong><span id='metaUVCategory'></span></div>"
    "<div><strong data-i18n='metadata_uv_peak'></strong><span id='metaUVPeak'></span></div>"
    "<div><strong data-i18n='metadata_season'></strong><span id='metaSeason'></span></div></div>"
    "<div class='species-actions' style='display:flex;flex-wrap:wrap;gap:12px;margin-top:12px;'><button id='exportSpecies' class='secondary' data-i18n='export_profiles'></button>"
    "<button id='importSpeciesBtn' class='secondary' data-i18n='import_profiles'></button><input id='importSpeciesFile' type='file' accept='.json,application/json' style='display:none'></div>"
    "<details><summary data-i18n='custom_profile'></summary><div><label data-i18n='profile_name'></label><input id='customName' placeholder='My species' data-i18n-placeholder='custom_name_hint'>"
    "<textarea id='customSchedule' rows='8' style='width:100%;border-radius:10px;padding:10px;background:rgba(255,255,255,0.08);color:#fefefe;' data-i18n-placeholder='custom_schedule_hint'></textarea>"
    "<label data-i18n='metadata_habitat'></label><input id='customHabitat' data-i18n-placeholder='metadata_habitat_hint'>"
    "<label data-i18n='metadata_uv_category'></label><input id='customUVCategory' data-i18n-placeholder='metadata_uv_category_hint'>"
    "<label data-i18n='metadata_uv_peak'></label><input id='customUVPeak' type='number' step='0.1' data-i18n-placeholder='metadata_uv_peak_hint'>"
    "<label data-i18n='metadata_season'></label><input id='customSeason' data-i18n-placeholder='metadata_season_hint'>"
    "<button id='saveCustom' data-i18n='save_custom'></button></div></details></section>"
    "<section><h2 data-i18n='light_control'></h2><div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;'>"
    "<div><label data-i18n='cct_day'></label><input id='cctDay' type='number' min='0' max='10000'><label data-i18n='cct_warm'></label><input id='cctWarm' type='number' min='0' max='10000'></div>"
    "<div><label data-i18n='uva_set'></label><input id='uvaSet' type='number' min='0' max='10000'><label data-i18n='uva_clamp'></label><input id='uvaClamp' type='number' min='0' max='10000'></div>"
    "<div><label data-i18n='uvb_set'></label><input id='uvbSet' type='number' min='0' max='10000'><label data-i18n='uvb_clamp'></label><input id='uvbClamp' type='number' min='0' max='10000'></div>"
    "<div><label data-i18n='uvb_period'></label><input id='uvbPeriod' type='number' min='1' max='255'><label data-i18n='uvb_duty'></label><input id='uvbDuty' type='number' min='0' max='10000'></div>"
    "</div><label data-i18n='sky_mode'></label><select id='skyMode'><option value='0'>Off</option><option value='1'>Blue</option><option value='2'>Aurora</option></select>"
    "<button id='applyLight' data-i18n='apply_light'></button></section>"
    "<section><h2 data-i18n='telemetry'></h2><div id='chartContainer'><canvas id='telemetryChart'></canvas></div>"
    "<table><thead><tr><th data-i18n='metric'></th><th data-i18n='value'></th></tr></thead><tbody id='telemetryTable'></tbody></table></section>"
    "<section><h2 data-i18n='ota_updates'></h2><div class='ota-block'><h3 data-i18n='controller_title'></h3><label data-i18n='controller_manifest'></label><input id='controllerManifest' type='file' accept='.json'><label data-i18n='controller_fw'></label><input id='controllerBin' type='file' accept='.bin'><progress id='controllerProgress' value='0' max='100'></progress><div class='ota-status-line'><strong data-i18n='ota_status_label'></strong><span id='controllerStatusText'>--</span></div><button id='flashController' data-i18n='flash_controller'></button></div><div class='ota-block'><h3 data-i18n='dome_title'></h3><label data-i18n='dome_manifest'></label><input id='domeManifest' type='file' accept='.json'><label data-i18n='dome_fw'></label><input id='domeBin' type='file' accept='.bin'><progress id='domeProgress' value='0' max='100'></progress><div class='ota-status-line'><strong data-i18n='ota_status_label'></strong><span id='domeStatusText'>--</span></div><button id='flashDome' data-i18n='flash_dome'></button></div></section>"
    "<section><h2 data-i18n='alarms'></h2><button id='toggleMute' data-i18n='mute_toggle'></button><div id='alarmState'></div></section>"
    "<section><h2 data-i18n='calibration'></h2><label data-i18n='uvi_max'></label><input id='calUviMax' type='number' step='0.1'><label data-i18n='cal_duty'></label><input id='calDuty' type='number'><label data-i18n='cal_measured'></label><input id='calMeasured' type='number' step='0.01'><button id='applyCalibration' data-i18n='apply_calibration'></button></section>"
    "<script>const I18N = {\n  fr: {\n    language: 'Langue',\n    species_profile: 'Profil d\\'esp\xe8ce',\n    apply_profile: 'Appliquer le profil',\n    custom_profile: 'Profil personnalis\xe9',\n    custom_profile_short: 'perso',\n    profile_name: 'Nom du profil',\n    custom_name_hint: 'Nom du profil personnalis\xe9',\n    save_custom: 'Enregistrer',\n    save_success: 'Profil enregistr\xe9',\n    save_error: '\xc9chec de l\\'enregistrement',\n    name_required: 'Nom requis',\n    invalid_json: 'JSON invalide',\n    species_error: 'Erreur esp\xe8ces',\n    status_error: 'Erreur statut',\n    custom_schedule_hint: 'JSON climate_schedule_t',\n    metadata_habitat: 'Habitat',\n    metadata_habitat_hint: 'Ex : For\xeat tropicale, d\xe9sert\u2026',\n    metadata_uv_category: 'Cat\xe9gorie UV',\n    metadata_uv_category_hint: 'Indice Ferguson / UVB',\n    metadata_uv_peak: 'Pic UV index',\n    metadata_uv_peak_hint: 'Valeur num\xe9rique (ex : 6.5)',\n    metadata_season: 'Cycle saisonnier',\n    metadata_season_hint: 'Ex : Saison s\xe8che / humide',\n    metadata_unknown: '\u2014',\n    profile_details: 'D\xe9tails du profil',\n    profile_common_name: 'Nom commun',\n    profile_type: 'Type',\n    export_profiles: 'Exporter profils',\n    import_profiles: 'Importer profils',\n    import_success: 'Import r\xe9ussi',\n    import_error: 'Import invalide',\n    light_control: 'Contr\xf4le lumineux',\n    cct_day: 'CCT Jour (\u2030)',\n    cct_warm: 'CCT Chaud (\u2030)',\n    uva_set: 'UVA consigne (\u2030)',\n    uva_clamp: 'UVA limite (\u2030)',\n    uvb_set: 'UVB consigne (\u2030)',\n    uvb_clamp: 'UVB limite (\u2030)',\n    uvb_period: 'P\xe9riode UVB (s)',\n    uvb_duty: 'Duty UVB (\u2030)',\n    sky_mode: 'Mode ciel',\n    apply_light: 'Appliquer',\n    telemetry: 'T\xe9l\xe9m\xe9tries en temps r\xe9el',\n    metric: 'Mesure',\n    value: 'Valeur',\n    ota_updates: 'Mises \xe0 jour OTA',\n    controller_title: 'Contr\xf4leur',\n    controller_manifest: 'Manifeste contr\xf4leur (.json sign\xe9)',\n    controller_fw: 'Firmware contr\xf4leur (.bin)',\n    flash_controller: 'Flasher contr\xf4leur',\n    dome_title: 'D\xf4me',\n    dome_manifest: 'Manifeste d\xf4me (.json sign\xe9)',\n    dome_fw: 'Firmware d\xf4me (.bin)',\n    flash_dome: 'Flasher d\xf4me',\n    ota_status_label: 'Statut OTA',\n    manifest_required: 'Manifeste requis',\n    firmware_required: 'Fichier firmware requis',\n    alarms: 'Alarmes',\n    mute_toggle: 'Basculer mute',\n    alarms_muted: 'Muet',\n    alarms_active: 'Actif',\n    apply_calibration: 'Enregistrer calibration',\n    calibration: 'Calibration UVB',\n    uvi_max: 'UVI cible',\n    cal_duty: 'Duty mesur\xe9 (\u2030)',\n    cal_measured: 'UVI mesur\xe9',\n    uvi_fault: 'Capteur UVI en d\xe9faut'\n  },\n  en: {\n    language: 'Language',\n    species_profile: 'Species profile',\n    apply_profile: 'Apply profile',\n    custom_profile: 'Custom profile',\n    custom_profile_short: 'custom',\n    profile_name: 'Profile name',\n    custom_name_hint: 'Custom profile name',\n    save_custom: 'Save custom profile',\n    save_success: 'Profile saved',\n    save_error: 'Save failed',\n    name_required: 'Name required',\n    invalid_json: 'Invalid JSON',\n    species_error: 'Species error',\n    status_error: 'Status error',\n    custom_schedule_hint: 'climate_schedule_t JSON payload',\n    metadata_habitat: 'Habitat',\n    metadata_habitat_hint: 'e.g. tropical forest, desert\u2026',\n    metadata_uv_category: 'UV category',\n    metadata_uv_category_hint: 'Ferguson zone / UV class',\n    metadata_uv_peak: 'UV index peak',\n    metadata_uv_peak_hint: 'Numeric value (e.g. 6.5)',\n    metadata_season: 'Seasonal cycle',\n    metadata_season_hint: 'e.g. Dry / wet season',\n    metadata_unknown: '\u2014',\n    profile_details: 'Profile details',\n    profile_common_name: 'Common name',\n    profile_type: 'Type',\n    export_profiles: 'Export profiles',\n    import_profiles: 'Import profiles',\n    import_success: 'Import succeeded',\n    import_error: 'Import failed',\n    light_control: 'Lighting control',\n    cct_day: 'CCT Day (\u2030)',\n    cct_warm: 'CCT Warm (\u2030)',\n    uva_set: 'UVA setpoint (\u2030)',\n    uva_clamp: 'UVA clamp (\u2030)',\n    uvb_set: 'UVB setpoint (\u2030)',\n    uvb_clamp: 'UVB clamp (\u2030)',\n    uvb_period: 'UVB period (s)',\n    uvb_duty: 'UVB duty (\u2030)',\n    sky_mode: 'Sky mode',\n    apply_light: 'Apply',\n    telemetry: 'Real-time telemetry',\n    metric: 'Metric',\n    value: 'Value',\n    ota_updates: 'OTA updates',\n    controller_title: 'Controller',\n    controller_manifest: 'Controller manifest (signed .json)',\n    controller_fw: 'Controller firmware (.bin)',\n    flash_controller: 'Flash controller',\n    dome_title: 'Dome',\n    dome_manifest: 'Dome manifest (signed .json)',\n    dome_fw: 'Dome firmware (.bin)',\n    flash_dome: 'Flash dome',\n    ota_status_label: 'OTA status',\n    manifest_required: 'Manifest required',\n    firmware_required: 'Firmware file required',\n    alarms: 'Alarms',\n    mute_toggle: 'Toggle mute',\n    alarms_muted: 'Muted',\n    alarms_active: 'Active',\n    apply_calibration: 'Apply calibration',\n    calibration: 'UVB calibration',\n    uvi_max: 'Target UVI',\n    cal_duty: 'Measured duty (\u2030)',\n    cal_measured: 'Measured UVI',\n    uvi_fault: 'UVI sensor fault'\n  },\n  es: {\n    language: 'Idioma',\n    species_profile: 'Perfil de especie',\n    apply_profile: 'Aplicar perfil',\n    custom_profile: 'Perfil personalizado',\n    custom_profile_short: 'personal',\n    profile_name: 'Nombre del perfil',\n    custom_name_hint: 'Nombre del perfil personalizado',\n    save_custom: 'Guardar personalizado',\n    save_success: 'Perfil guardado',\n    save_error: 'Error al guardar',\n    name_required: 'Nombre requerido',\n    invalid_json: 'JSON inv\xe1lido',\n    species_error: 'Error especies',\n    status_error: 'Error estado',\n    custom_schedule_hint: 'JSON climate_schedule_t',\n    metadata_habitat: 'H\xe1bitat',\n    metadata_habitat_hint: 'p.ej. bosque tropical, desierto\u2026',\n    metadata_uv_category: 'Categor\xeda UV',\n    metadata_uv_category_hint: 'Zona Ferguson / clase UV',\n    metadata_uv_peak: 'Pico de \xedndice UV',\n    metadata_uv_peak_hint: 'Valor num\xe9rico (p.ej. 6.5)',\n    metadata_season: 'Ciclo estacional',\n    metadata_season_hint: 'p.ej. Estaci\xf3n seca / h\xfameda',\n    metadata_unknown: '\u2014',\n    profile_details: 'Detalles del perfil',\n    profile_common_name: 'Nombre com\xfan',\n    profile_type: 'Tipo',\n    export_profiles: 'Exportar perfiles',\n    import_profiles: 'Importar perfiles',\n    import_success: 'Importaci\xf3n correcta',\n    import_error: 'Importaci\xf3n fallida',\n    light_control: 'Control lum\xednico',\n    cct_day: 'CCT D\xeda (\u2030)',\n    cct_warm: 'CCT C\xe1lido (\u2030)',\n    uva_set: 'UVA consigna (\u2030)',\n    uva_clamp: 'UVA l\xedmite (\u2030)',\n    uvb_set: 'UVB consigna (\u2030)',\n    uvb_clamp: 'UVB l\xedmite (\u2030)',\n    uvb_period: 'Periodo UVB (s)',\n    uvb_duty: 'Duty UVB (\u2030)',\n    sky_mode: 'Modo cielo',\n    apply_light: 'Aplicar',\n    telemetry: 'Telemetr\xeda en tiempo real',\n    metric: 'M\xe9trica',\n    value: 'Valor',\n    ota_updates: 'Actualizaciones OTA',\n    controller_title: 'Controlador',\n    controller_manifest: 'Manifiesto controlador (.json firmado)',\n    controller_fw: 'Firmware controlador (.bin)',\n    flash_controller: 'Flashear controlador',\n    dome_title: 'C\xfapula',\n    dome_manifest: 'Manifiesto c\xfapula (.json firmado)',\n    dome_fw: 'Firmware c\xfapula (.bin)',\n    flash_dome: 'Flashear c\xfapula',\n    ota_status_label: 'Estado OTA',\n    manifest_required: 'Manifiesto requerido',\n    firmware_required: 'Archivo firmware requerido',\n    alarms: 'Alarmas',\n    mute_toggle: 'Alternar mute',\n    alarms_muted: 'Silenciado',\n    alarms_active: 'Activo',\n    apply_calibration: 'Guardar calibraci\xf3n',\n    calibration: 'Calibraci\xf3n UVB',\n    uvi_max: 'UVI objetivo',\n    cal_duty: 'Duty medido (\u2030)',\n    cal_measured: 'UVI medido',\n    uvi_fault: 'Sensor UVI en fallo'\n  },\n  de: {\n    language: 'Sprache',\n    species_profile: 'Artprofil',\n    apply_profile: 'Profil anwenden',\n    custom_profile: 'Benutzerprofil',\n    custom_profile_short: 'benutzer',\n    profile_name: 'Profilname',\n    custom_name_hint: 'Name des Benutzerprofils',\n    save_custom: 'Profil speichern',\n    save_success: 'Profil gespeichert',\n    save_error: 'Speichern fehlgeschlagen',\n    name_required: 'Name erforderlich',\n    invalid_json: 'Ung\xfcltiges JSON',\n    species_error: 'Artenfehler',\n    status_error: 'Statusfehler',\n    custom_schedule_hint: 'climate_schedule_t JSON',\n    metadata_habitat: 'Lebensraum',\n    metadata_habitat_hint: 'z.B. Tropenwald, W\xfcste\u2026',\n    metadata_uv_category: 'UV-Kategorie',\n    metadata_uv_category_hint: 'Ferguson-Zone / UV-Klasse',\n    metadata_uv_peak: 'UV-Index Spitze',\n    metadata_uv_peak_hint: 'Zahlenwert (z.B. 6.5)',\n    metadata_season: 'Jahreszyklus',\n    metadata_season_hint: 'z.B. Trocken- / Regenzeit',\n    metadata_unknown: '\u2014',\n    profile_details: 'Profildetails',\n    profile_common_name: 'Trivialname',\n    profile_type: 'Typ',\n    export_profiles: 'Profile exportieren',\n    import_profiles: 'Profile importieren',\n    import_success: 'Import erfolgreich',\n    import_error: 'Import fehlgeschlagen',\n    light_control: 'Lichtsteuerung',\n    cct_day: 'CCT Tag (\u2030)',\n    cct_warm: 'CCT Warm (\u2030)',\n    uva_set: 'UVA Sollwert (\u2030)',\n    uva_clamp: 'UVA Begrenzung (\u2030)',\n    uvb_set: 'UVB Sollwert (\u2030)',\n    uvb_clamp: 'UVB Begrenzung (\u2030)',\n    uvb_period: 'UVB Periode (s)',\n    uvb_duty: 'UVB Duty (\u2030)',\n    sky_mode: 'Himmelmodus',\n    apply_light: 'Anwenden',\n    telemetry: 'Live-Telemetrie',\n    metric: 'Messwert',\n    value: 'Wert',\n    ota_updates: 'OTA-Updates',\n    controller_title: 'Controller',\n    controller_manifest: 'Controller-Manifest (.json signiert)',\n    controller_fw: 'Controller-Firmware (.bin)',\n    flash_controller: 'Controller flashen',\n    dome_title: 'Dom',\n    dome_manifest: 'Dom-Manifest (.json signiert)',\n    dome_fw: 'Dom-Firmware (.bin)',\n    flash_dome: 'Dom flashen',\n    ota_status_label: 'OTA-Status',\n    manifest_required: 'Manifest erforderlich',\n    firmware_required: 'Firmware-Datei erforderlich',\n    alarms: 'Alarme',\n    mute_toggle: 'Stummschalten',\n    alarms_muted: 'Stumm',\n    alarms_active: 'Aktiv',\n    apply_calibration: 'Kalibrierung speichern',\n    calibration: 'UVB-Kalibrierung',\n    uvi_max: 'Ziel-UVI',\n    cal_duty: 'Gemessene Duty (\u2030)',\n    cal_measured: 'Gemessener UVI',\n    uvi_fault: 'UVI-Sensorfehler'\n  },\n  it: {\n    language: 'Lingua',\n    species_profile: 'Profilo specie',\n    apply_profile: 'Applica profilo',\n    custom_profile: 'Profilo personalizzato',\n    custom_profile_short: 'personal',\n    profile_name: 'Nome profilo',\n    custom_name_hint: 'Nome profilo personalizzato',\n    save_custom: 'Salva profilo',\n    save_success: 'Profilo salvato',\n    save_error: 'Salvataggio fallito',\n    name_required: 'Nome richiesto',\n    invalid_json: 'JSON non valido',\n    species_error: 'Errore specie',\n    status_error: 'Errore stato',\n    custom_schedule_hint: 'payload JSON climate_schedule_t',\n    metadata_habitat: 'Habitat',\n    metadata_habitat_hint: 'es. foresta tropicale, deserto\u2026',\n    metadata_uv_category: 'Categoria UV',\n    metadata_uv_category_hint: 'Zona Ferguson / classe UV',\n    metadata_uv_peak: 'Picco indice UV',\n    metadata_uv_peak_hint: 'Valore numerico (es. 6.5)',\n    metadata_season: 'Ciclo stagionale',\n    metadata_season_hint: 'es. stagione secca / piovosa',\n    metadata_unknown: '\u2014',\n    profile_details: 'Dettagli profilo',\n    profile_common_name: 'Nome comune',\n    profile_type: 'Tipo',\n    export_profiles: 'Esporta profili',\n    import_profiles: 'Importa profili',\n    import_success: 'Import riuscito',\n    import_error: 'Import fallito',\n    light_control: 'Controllo luci',\n    cct_day: 'CCT Giorno (\u2030)',\n    cct_warm: 'CCT Caldo (\u2030)',\n    uva_set: 'UVA setpoint (\u2030)',\n    uva_clamp: 'UVA limite (\u2030)',\n    uvb_set: 'UVB setpoint (\u2030)',\n    uvb_clamp: 'UVB limite (\u2030)',\n    uvb_period: 'Periodo UVB (s)',\n    uvb_duty: 'Duty UVB (\u2030)',\n    sky_mode: 'Modalit\xe0 cielo',\n    apply_light: 'Applica',\n    telemetry: 'Telemetria in tempo reale',\n    metric: 'Parametro',\n    value: 'Valore',\n    ota_updates: 'Aggiornamenti OTA',\n    controller_title: 'Controller',\n    controller_manifest: 'Manifest controller (.json firmato)',\n    controller_fw: 'Firmware controller (.bin)',\n    flash_controller: 'Flash controller',\n    dome_title: 'Cupola',\n    dome_manifest: 'Manifest cupola (.json firmato)',\n    dome_fw: 'Firmware cupola (.bin)',\n    flash_dome: 'Flash cupola',\n    ota_status_label: 'Stato OTA',\n    manifest_required: 'Manifest richiesto',\n    firmware_required: 'File firmware richiesto',\n    alarms: 'Allarmi',\n    mute_toggle: 'Attiva/disattiva mute',\n    alarms_muted: 'Silenzioso',\n    alarms_active: 'Attivo',\n    apply_calibration: 'Applica calibrazione',\n    calibration: 'Calibrazione UVB',\n    uvi_max: 'UVI target',\n    cal_duty: 'Duty misurato (\u2030)',\n    cal_measured: 'UVI misurato',\n    uvi_fault: 'Sensore UVI in errore'\n  }\n};\n\nconst LANGUAGE_NAMES = {\n  fr: 'Fran\xe7ais',\n  en: 'English',\n  es: 'Espa\xf1ol',\n  de: 'Deutsch',\n  it: 'Italiano'\n};\n\nconst DEFAULT_LANG = 'fr';\nlet lang = DEFAULT_LANG;\n\nconst speciesState = {\n  builtin: [],\n  custom: [],\n  builtinMap: new Map(),\n  customMap: new Map(),\n  locales: [],\n  activeKey: null\n};\n\nconst banner = document.getElementById('statusBanner');\n\nfunction translations() {\n  return I18N[lang] || I18N[DEFAULT_LANG];\n}\n\nfunction applyTranslations() {\n  const dict = translations();\n  document.querySelectorAll('[data-i18n]').forEach(el => {\n    const key = el.getAttribute('data-i18n');\n    if (dict[key]) {\n      el.textContent = dict[key];\n    }\n  });\n  document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {\n    const key = el.getAttribute('data-i18n-placeholder');\n    if (dict[key]) {\n      el.setAttribute('placeholder', dict[key]);\n    }\n  });\n}\n\nfunction setLang(newLang) {\n  const normalized = (newLang || DEFAULT_LANG).toLowerCase();\n  lang = I18N[normalized] ? normalized : DEFAULT_LANG;\n  const select = document.getElementById('languageSelect');\n  if (select && select.value !== lang) {\n    select.value = lang;\n  }\n  applyTranslations();\n  renderSpeciesOptions(speciesState.activeKey);\n  updateSpeciesMetadata(speciesState.activeKey);\n}\n\nasync function fetchJSON(url, opts) {\n  const response = await fetch(url, opts);\n  if (!response.ok) {\n    const text = await response.text();\n    throw new Error(text || response.statusText);\n  }\n  if (response.status === 204) {\n    return {};\n  }\n  return response.json();\n}\n\nfunction permilleFromReg(v) {\n  return v * 40;\n}\n\nfunction regFromPermille(p) {\n  return Math.min(255, Math.max(0, Math.round(p / 40)));\n}\n\nfunction encodeManifest(text) {\n  return btoa(unescape(encodeURIComponent(text)));\n}\n\nfunction describeOta(entry) {\n  if (!entry) {\n    return '--';\n  }\n  const parts = [];\n  if (entry.version) {\n    parts.push(entry.version);\n  }\n  if (entry.message) {\n    parts.push(entry.message);\n  } else if (entry.state) {\n    parts.push(entry.state);\n  }\n  if (entry.sha256) {\n    parts.push(entry.sha256.slice(0, 8) + '\u2026');\n  }\n  return parts.join(' \u2022 ');\n}\n\nfunction updateBanner(text, isError) {\n  banner.textContent = text;\n  banner.className = isError ? 'error' : 'ok';\n}\n\nconst chartCtx = document.getElementById('telemetryChart').getContext('2d');\nconst chartState = { points: [] };\n\nfunction renderChart() {\n  const ctx = chartCtx;\n  const width = ctx.canvas.width;\n  const height = ctx.canvas.height;\n  ctx.clearRect(0, 0, width, height);\n  if (chartState.points.length === 0) {\n    return;\n  }\n  ctx.strokeStyle = '#2dd4ff';\n  ctx.lineWidth = 2;\n  ctx.beginPath();\n  chartState.points.forEach((point, index) => {\n    const x = width * (index / (chartState.points.length - 1 || 1));\n    const y = height * (1 - point.tempNorm);\n    if (index === 0) {\n      ctx.moveTo(x, y);\n    } else {\n      ctx.lineTo(x, y);\n    }\n  });\n  ctx.stroke();\n  ctx.strokeStyle = '#fbbf24';\n  ctx.beginPath();\n  chartState.points.forEach((point, index) => {\n    const x = width * (index / (chartState.points.length - 1 || 1));\n    const y = height * (1 - point.humNorm);\n    if (index === 0) {\n      ctx.moveTo(x, y);\n    } else {\n      ctx.lineTo(x, y);\n    }\n  });\n  ctx.stroke();\n}\n\nfunction updateLanguageOptions(localeList) {\n  const select = document.getElementById('languageSelect');\n  if (!select) {\n    return;\n  }\n  const dict = translations();\n  const seen = new Set();\n  select.innerHTML = '';\n  const entries = Array.isArray(localeList) && localeList.length ? localeList : Object.keys(I18N);\n  entries.forEach(code => {\n    if (!code) {\n      return;\n    }\n    const lower = code.toLowerCase();\n    if (seen.has(lower)) {\n      return;\n    }\n    seen.add(lower);\n    const option = document.createElement('option');\n    option.value = lower;\n    option.textContent = LANGUAGE_NAMES[lower] || lower.toUpperCase();\n    select.appendChild(option);\n  });\n  if (!seen.has(lang)) {\n    lang = seen.has(DEFAULT_LANG) ? DEFAULT_LANG : Array.from(seen)[0];\n  }\n  select.value = lang;\n  applyTranslations();\n}\n\nfunction determineLabel(profile) {\n  if (!profile) {\n    return '';\n  }\n  if (profile.labels) {\n    if (profile.labels[lang]) {\n      return profile.labels[lang];\n    }\n    if (profile.labels[DEFAULT_LANG]) {\n      return profile.labels[DEFAULT_LANG];\n    }\n    if (profile.labels.en) {\n      return profile.labels.en;\n    }\n    const keys = Object.keys(profile.labels);\n    if (keys.length) {\n      return profile.labels[keys[0]];\n    }\n  }\n  return profile.name || profile.key;\n}\n\nfunction renderSpeciesOptions(activeKey) {\n  const select = document.getElementById('speciesSelect');\n  if (!select) {\n    return;\n  }\n  const dict = translations();\n  select.innerHTML = '';\n  const customSuffix = dict.custom_profile_short || 'custom';\n  speciesState.builtin.forEach(profile => {\n    const option = document.createElement('option');\n    option.value = profile.key;\n    option.textContent = determineLabel(profile);\n    select.appendChild(option);\n  });\n  speciesState.custom.forEach(profile => {\n    const option = document.createElement('option');\n    option.value = profile.key;\n    option.textContent = `${profile.name} (${customSuffix})`;\n    select.appendChild(option);\n  });\n  let selected = activeKey;\n  if (!selected || (!speciesState.builtinMap.has(selected) && !speciesState.customMap.has(selected))) {\n    selected = select.options.length ? select.options[0].value : '';\n  }\n  if (selected) {\n    select.value = selected;\n  }\n  speciesState.activeKey = selected;\n}\n\nfunction updateSpeciesMetadata(key) {\n  const container = document.getElementById('speciesMetadata');\n  if (!container) {\n    return;\n  }\n  const dict = translations();\n  const profile = speciesState.builtinMap.get(key) || speciesState.customMap.get(key);\n  if (!profile) {\n    container.style.display = 'none';\n    return;\n  }\n  container.style.display = '';\n  const isCustom = speciesState.customMap.has(key);\n  const metadata = profile.metadata || {};\n  const unknown = dict.metadata_unknown || '\u2014';\n  document.getElementById('metaName').textContent = isCustom ? profile.name : determineLabel(profile);\n  document.getElementById('metaType').textContent = isCustom ? (dict.custom_profile || 'Custom profile') : (dict.species_profile || 'Species profile');\n  document.getElementById('metaHabitat').textContent = metadata.habitat || unknown;\n  document.getElementById('metaUVCategory').textContent = metadata.uv_index_category || unknown;\n  const peak = typeof metadata.uv_index_peak === 'number' ? metadata.uv_index_peak : (typeof profile.uv_index_peak === 'number' ? profile.uv_index_peak : null);\n  document.getElementById('metaUVPeak').textContent = peak != null ? peak.toFixed(1) : unknown;\n  document.getElementById('metaSeason').textContent = metadata.season_cycle || unknown;\n}\n\nfunction metadataFromForm() {\n  const meta = {};\n  const habitat = document.getElementById('customHabitat').value.trim();\n  const category = document.getElementById('customUVCategory').value.trim();\n  const season = document.getElementById('customSeason').value.trim();\n  const peakText = document.getElementById('customUVPeak').value.trim();\n  if (habitat) {\n    meta.habitat = habitat;\n  }\n  if (category) {\n    meta.uv_index_category = category;\n  }\n  if (season) {\n    meta.season_cycle = season;\n  }\n  if (peakText) {\n    const peak = parseFloat(peakText);\n    if (!Number.isNaN(peak)) {\n      meta.uv_index_peak = peak;\n    }\n  }\n  return meta;\n}\n\n\nasync function fetchAllSpecies() {\n  const perPage = 16;\n  const locales = new Set();\n  const builtin = [];\n  const custom = [];\n  let builtinTotal = 0;\n  let customTotal = 0;\n\n  async function fetchPage(bPage, cPage) {\n    const params = new URLSearchParams({\n      builtin_page: String(bPage),\n      custom_page: String(cPage),\n      builtin_per_page: String(perPage),\n      custom_per_page: String(perPage)\n    });\n    return fetchJSON('/api/species?' + params.toString());\n  }\n\n  const first = await fetchPage(0, 0);\n  if (Array.isArray(first.locales)) {\n    first.locales.forEach(code => locales.add(code.toLowerCase()));\n  }\n  if (first.builtin && Array.isArray(first.builtin.items)) {\n    builtin.push(...first.builtin.items);\n    builtinTotal = first.builtin.total || builtin.length;\n  }\n  if (first.custom && Array.isArray(first.custom.items)) {\n    custom.push(...first.custom.items);\n    customTotal = first.custom.total || custom.length;\n  }\n\n  let page = 1;\n  while (builtin.length < builtinTotal && page < 32) {\n    const resp = await fetchPage(page, 0);\n    if (resp.builtin && Array.isArray(resp.builtin.items)) {\n      builtin.push(...resp.builtin.items);\n    }\n    if (Array.isArray(resp.locales)) {\n      resp.locales.forEach(code => locales.add(code.toLowerCase()));\n    }\n    page += 1;\n  }\n\n  page = 1;\n  while (custom.length < customTotal && page < 64) {\n    const resp = await fetchPage(0, page);\n    if (resp.custom && Array.isArray(resp.custom.items)) {\n      custom.push(...resp.custom.items);\n    }\n    if (Array.isArray(resp.locales)) {\n      resp.locales.forEach(code => locales.add(code.toLowerCase()));\n    }\n    page += 1;\n  }\n\n  if (!locales.size) {\n    locales.add(DEFAULT_LANG);\n    locales.add('en');\n  }\n\n  return {\n    builtin,\n    custom,\n    locales: Array.from(locales),\n    active_key: first.active_key || (builtin.length ? builtin[0].key : '')\n  };\n}\n\nasync function refreshSpecies() {\n  const dict = translations();\n  try {\n    const data = await fetchAllSpecies();\n    speciesState.builtin = data.builtin;\n    speciesState.custom = data.custom;\n    speciesState.builtinMap = new Map(data.builtin.map(profile => [profile.key, profile]));\n    speciesState.customMap = new Map(data.custom.map(profile => [profile.key, profile]));\n    speciesState.locales = data.locales;\n    speciesState.activeKey = data.active_key;\n    updateLanguageOptions(data.locales);\n    renderSpeciesOptions(speciesState.activeKey);\n    updateSpeciesMetadata(speciesState.activeKey);\n  } catch (err) {\n    alert((dict.species_error || 'Species error') + ': ' + err.message);\n  }\n}\n\nasync function refreshStatus() {\n  const dict = translations();\n  try {\n    const status = await fetchJSON('/api/status');\n    updateBanner(status.summary, false);\n    document.getElementById('cctDay').value = status.light.cct.day;\n    document.getElementById('cctWarm').value = status.light.cct.warm;\n    document.getElementById('uvaSet').value = status.light.uva.set;\n    document.getElementById('uvaClamp').value = status.light.uva.clamp;\n    document.getElementById('uvbSet').value = status.light.uvb.set;\n    document.getElementById('uvbClamp').value = status.light.uvb.clamp;\n    document.getElementById('uvbPeriod').value = status.light.uvb.period_s;\n    document.getElementById('uvbDuty').value = status.light.uvb.duty_pm;\n    document.getElementById('skyMode').value = status.light.sky;\n    document.getElementById('alarmState').textContent = status.alarms.muted ? (dict.alarms_muted || 'Muted') : (dict.alarms_active || 'Active');\n    document.getElementById('calUviMax').value = status.calibration.uvi_max.toFixed(2);\n    document.getElementById('calDuty').value = status.calibration.last_duty_pm.toFixed(0);\n    document.getElementById('calMeasured').value = status.calibration.last_uvi.toFixed(2);\n\n    const table = document.getElementById('telemetryTable');\n    table.innerHTML = '';\n    const uviValid = status.climate && status.climate.uvi_valid;\n    const uviFault = status.dome && status.dome.uvi_fault;\n    let uviText = '--';\n    if (uviValid) {\n      uviText = `${status.climate.uvi_measured.toFixed(2)} (\u0394 ${status.climate.uvi_error.toFixed(2)}, ${status.climate.irradiance_uW_cm2.toFixed(1)} \xb5W/cm\xb2)`;\n    } else if (uviFault) {\n      uviText = dict.uvi_fault || 'sensor fault';\n    } else if (status.env.uvi !== undefined) {\n      uviText = status.env.uvi.toFixed(2);\n    }\n    const irrText = status.env.irradiance_uW_cm2 !== undefined ? status.env.irradiance_uW_cm2.toFixed(1) : '--';\n    const rows = [\n      ['Temp \xb0C', status.env.temperature.toFixed(1)],\n      ['Hum %', status.env.humidity.toFixed(1)],\n      ['Press hPa', status.env.pressure.toFixed(1)],\n      ['UVI', uviText],\n      ['Irr \xb5W/cm\xb2', irrText],\n      ['Fan %', permilleFromReg(status.light.fan_pwm).toFixed(0)]\n    ];\n    rows.forEach(([label, value]) => {\n      const tr = document.createElement('tr');\n      const td1 = document.createElement('td');\n      td1.textContent = label;\n      const td2 = document.createElement('td');\n      td2.textContent = value;\n      tr.appendChild(td1);\n      tr.appendChild(td2);\n      table.appendChild(tr);\n    });\n\n    const ota = status.ota || {};\n    const controllerStatus = document.getElementById('controllerStatusText');\n    if (controllerStatus) {\n      controllerStatus.textContent = describeOta(ota.controller);\n    }\n    const domeStatus = document.getElementById('domeStatusText');\n    if (domeStatus) {\n      domeStatus.textContent = describeOta(ota.dome);\n    }\n\n    chartState.points.push({\n      tempNorm: Math.min(1, Math.max(0, (status.env.temperature - 10) / 30)),\n      humNorm: Math.min(1, Math.max(0, status.env.humidity / 100))\n    });\n    if (chartState.points.length > 120) {\n      chartState.points.shift();\n    }\n    renderChart();\n  } catch (err) {\n    updateBanner((dict.status_error || 'Status error') + ': ' + err.message, true);\n  }\n}\n\nasync function applySpeciesProfile() {\n  const dict = translations();\n  const key = document.getElementById('speciesSelect').value;\n  await fetchJSON('/api/species/apply', {\n    method: 'POST',\n    headers: { 'Content-Type': 'application/json' },\n    body: JSON.stringify({ key })\n  });\n  speciesState.activeKey = key;\n  updateSpeciesMetadata(key);\n}\n\nfunction parseCustomSchedule() {\n  const text = document.getElementById('customSchedule').value;\n  if (!text.trim()) {\n    throw new Error('Empty schedule');\n  }\n  return JSON.parse(text);\n}\n\nasync function saveCustomProfile() {\n  const dict = translations();\n  const name = document.getElementById('customName').value.trim();\n  if (!name) {\n    alert(dict.name_required || 'Name required');\n    return;\n  }\n  let schedule;\n  try {\n    schedule = parseCustomSchedule();\n  } catch (err) {\n    alert((dict.invalid_json || 'Invalid JSON') + ': ' + err.message);\n    return;\n  }\n  const metadata = metadataFromForm();\n  const payload = { name, schedule };\n  if (Object.keys(metadata).length) {\n    payload.metadata = metadata;\n  }\n  try {\n    await fetchJSON('/api/species/custom', {\n      method: 'POST',\n      headers: { 'Content-Type': 'application/json' },\n      body: JSON.stringify(payload)\n    });\n    await refreshSpecies();\n  } catch (err) {\n    alert((dict.save_error || 'Save failed') + ': ' + err.message);\n  }\n}\n\nasync function toggleMute() {\n  await fetchJSON('/api/alarms/mute', {\n    method: 'POST',\n    headers: { 'Content-Type': 'application/json' },\n    body: JSON.stringify({ toggle: true })\n  });\n  await refreshStatus();\n}\n\nasync function exportSpecies() {\n  const data = await fetchJSON('/api/species/export');\n  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });\n  const url = URL.createObjectURL(blob);\n  const link = document.createElement('a');\n  link.href = url;\n  link.download = 'species_profiles.json';\n  document.body.appendChild(link);\n  link.click();\n  document.body.removeChild(link);\n  URL.revokeObjectURL(url);\n}\n\nasync function importSpecies(dataFile) {\n  const dict = translations();\n  const text = await dataFile.text();\n  const payload = JSON.parse(text);\n  await fetchJSON('/api/species/import', {\n    method: 'POST',\n    headers: { 'Content-Type': 'application/json' },\n    body: JSON.stringify(payload)\n  });\n  alert(dict.import_success || 'Import succeeded');\n  await refreshSpecies();\n}\n\nasync function uploadFirmware(manifestId, binId, url, progressId) {\n  const dict = translations();\n  const manifestFile = document.getElementById(manifestId).files[0];\n  if (!manifestFile) {\n    alert(dict.manifest_required || 'Manifest required');\n    return;\n  }\n  const firmware = document.getElementById(binId).files[0];\n  if (!firmware) {\n    alert(dict.firmware_required || 'Firmware required');\n    return;\n  }\n  const manifestText = await manifestFile.text();\n  const progress = document.getElementById(progressId);\n  if (progress) {\n    progress.value = 0;\n  }\n  const response = await fetch(url, {\n    method: 'POST',\n    headers: {\n      'X-OTA-Manifest': encodeManifest(manifestText),\n      'Content-Type': 'application/octet-stream'\n    },\n    body: firmware\n  });\n  if (!response.ok) {\n    throw new Error(await response.text());\n  }\n  if (progress) {\n    progress.value = 100;\n  }\n  try {\n    await response.json();\n  } catch (err) {\n    /* ignore */\n  }\n  await refreshStatus();\n}\n\n// Event wiring\nconst languageSelect = document.getElementById('languageSelect');\nif (languageSelect) {\n  languageSelect.addEventListener('change', event => {\n    setLang(event.target.value);\n  });\n}\n\ndocument.getElementById('speciesSelect').addEventListener('change', event => {\n  speciesState.activeKey = event.target.value;\n  updateSpeciesMetadata(speciesState.activeKey);\n});\n\ndocument.getElementById('applyLight').addEventListener('click', async () => {\n  const payload = {\n    cct: {\n      day: +document.getElementById('cctDay').value,\n      warm: +document.getElementById('cctWarm').value\n    },\n    uva: {\n      set: +document.getElementById('uvaSet').value,\n      clamp: +document.getElementById('uvaClamp').value\n    },\n    uvb: {\n      set: +document.getElementById('uvbSet').value,\n      clamp: +document.getElementById('uvbClamp').value,\n      period_s: +document.getElementById('uvbPeriod').value,\n      duty_pm: +document.getElementById('uvbDuty').value\n    },\n    sky: +document.getElementById('skyMode').value\n  };\n  await fetchJSON('/api/light/dome0', {\n    method: 'POST',\n    headers: { 'Content-Type': 'application/json' },\n    body: JSON.stringify(payload)\n  });\n  await refreshStatus();\n});\n\ndocument.getElementById('applySpecies').addEventListener('click', () => {\n  applySpeciesProfile().catch(err => alert(err.message));\n});\n\ndocument.getElementById('saveCustom').addEventListener('click', () => {\n  saveCustomProfile();\n});\n\ndocument.getElementById('toggleMute').addEventListener('click', () => {\n  toggleMute().catch(err => alert(err.message));\n});\n\ndocument.getElementById('applyCalibration').addEventListener('click', async () => {\n  await fetchJSON('/api/calibrate/uvb', {\n    method: 'POST',\n    headers: { 'Content-Type': 'application/json' },\n    body: JSON.stringify({\n      duty_pm: +document.getElementById('calDuty').value,\n      uvi: +document.getElementById('calMeasured').value,\n      uvi_max: +document.getElementById('calUviMax').value\n    })\n  });\n  await refreshStatus();\n});\n\ndocument.getElementById('exportSpecies').addEventListener('click', () => {\n  exportSpecies().catch(err => alert(err.message));\n});\n\nconst importInput = document.getElementById('importSpeciesFile');\ndocument.getElementById('importSpeciesBtn').addEventListener('click', () => {\n  importInput.click();\n});\n\nimportInput.addEventListener('change', event => {\n  const file = event.target.files[0];\n  if (!file) {\n    return;\n  }\n  importSpecies(file).catch(err => alert((translations().import_error || 'Import failed') + ': ' + err.message)).finally(() => {\n    event.target.value = '';\n  });\n});\n\ndocument.getElementById('flashController').addEventListener('click', () => {\n  uploadFirmware('controllerManifest', 'controllerBin', '/api/ota/controller', 'controllerProgress').catch(err => alert(err.message));\n});\n\ndocument.getElementById('flashDome').addEventListener('click', () => {\n  uploadFirmware('domeManifest', 'domeBin', '/api/ota/dome', 'domeProgress').catch(err => alert(err.message));\n});\n\nsetLang(DEFAULT_LANG);\nrefreshSpecies();\nrefreshStatus();\nsetInterval(refreshStatus, 5000);</script></body></html>";

static float permille_from_reg(uint8_t reg_value)
{
    return (float)reg_value * 40.0f;
}

static uint8_t reg_from_permille(float value)
{
    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 10000.0f) {
        value = 10000.0f;
    }
    return (uint8_t)((value + 20.0f) / 40.0f);
}

static uint16_t rd16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void wr16_le(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)(value >> 8);
}

static esp_err_t read_dome_status(cJSON *root)
{
    uint8_t status = 0;
    uint8_t cct_buf[DOME_REG_BLOCK_CCT_LEN] = {0};
    uint8_t uva_buf[DOME_REG_BLOCK_UVA_LEN] = {0};
    uint8_t uvb_buf[DOME_REG_BLOCK_UVB_LEN] = {0};
    uint8_t fan_buf[DOME_REG_BLOCK_FAN_LEN] = {0};
    uint8_t uvi_buf[DOME_REG_BLOCK_UVI_LEN] = {0};
    uint8_t heat_buf[DOME_REG_BLOCK_HEATSINK_LEN] = {0};
    uint8_t diag_buf[DOME_REG_BLOCK_DIAG_LEN] = {0};

    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_STATUS, &status, 1), TAG, "status read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_BLOCK_CCT, cct_buf, sizeof(cct_buf)), TAG, "cct read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_BLOCK_UVA, uva_buf, sizeof(uva_buf)), TAG, "uva read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_BLOCK_UVB, uvb_buf, sizeof(uvb_buf)), TAG, "uvb read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_SKY_CFG, &fan_buf[0], 1), TAG, "sky read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_BLOCK_FAN, fan_buf, sizeof(fan_buf)), TAG, "fan read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_BLOCK_UVI, uvi_buf, sizeof(uvi_buf)), TAG, "uvi read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_BLOCK_HEATSINK, heat_buf, sizeof(heat_buf)), TAG, "heatsink read");
    ESP_RETURN_ON_ERROR(dome_bus_read(DOME_REG_BLOCK_DIAG, diag_buf, sizeof(diag_buf)), TAG, "diag read");

    cJSON *light = cJSON_AddObjectToObject(root, "light");
    cJSON *cct = cJSON_AddObjectToObject(light, "cct");
    cJSON_AddNumberToObject(cct, "day", rd16_le(&cct_buf[0]));
    cJSON_AddNumberToObject(cct, "warm", rd16_le(&cct_buf[2]));

    cJSON *uva = cJSON_AddObjectToObject(light, "uva");
    cJSON_AddNumberToObject(uva, "set", rd16_le(&uva_buf[0]));
    cJSON_AddNumberToObject(uva, "clamp", rd16_le(&uva_buf[2]));

    cJSON *uvb = cJSON_AddObjectToObject(light, "uvb");
    cJSON_AddNumberToObject(uvb, "set", permille_from_reg(uvb_buf[1]));
    cJSON_AddNumberToObject(uvb, "clamp", permille_from_reg(uvb_buf[2]));
    cJSON_AddNumberToObject(uvb, "period_s", uvb_buf[0]);
    cJSON_AddNumberToObject(uvb, "duty_pm", permille_from_reg(uvb_buf[1]));

    cJSON_AddNumberToObject(light, "sky", fan_buf[0]);
    cJSON_AddNumberToObject(light, "fan_pwm", (float)rd16_le(&fan_buf[1]) * 100.0f / 4095.0f);

    cJSON *dome = cJSON_AddObjectToObject(root, "dome");
    cJSON_AddNumberToObject(dome, "status", status);
    cJSON_AddNumberToObject(dome, "flags", heat_buf[1]);
    int8_t heat = (int8_t)heat_buf[0];
    cJSON_AddNumberToObject(dome, "heatsink_c", (float)heat);
    float irradiance = (float)rd16_le(&uvi_buf[0]) / 256.0f;
    float uvi = (float)rd16_le(&uvi_buf[2]) / 256.0f;
    cJSON_AddNumberToObject(dome, "uvi", uvi);
    cJSON_AddNumberToObject(dome, "irradiance_uW_cm2", irradiance);
    cJSON_AddBoolToObject(dome, "uvi_fault", (status & ST_UVI_FAULT) != 0);

    uint16_t i2c_errors = rd16_le(&diag_buf[DOME_REG_DIAG_I2C_ERR_L - DOME_REG_BLOCK_DIAG]);
    uint16_t pwm_errors = rd16_le(&diag_buf[DOME_REG_DIAG_PWM_ERR_L - DOME_REG_BLOCK_DIAG]);
    uint16_t interlocks = rd16_le(&diag_buf[DOME_REG_DIAG_INT_COUNT_L - DOME_REG_BLOCK_DIAG]);
    uint8_t uv_total = diag_buf[DOME_REG_DIAG_UV_EVENT_COUNT - DOME_REG_BLOCK_DIAG];
    uint8_t uv_head = diag_buf[DOME_REG_DIAG_UV_EVENT_HEAD - DOME_REG_BLOCK_DIAG];

    cJSON *diag = cJSON_AddObjectToObject(dome, "diagnostics");
    if (diag) {
        cJSON_AddNumberToObject(diag, "i2c_errors", i2c_errors);
        cJSON_AddNumberToObject(diag, "pwm_errors", pwm_errors);
        cJSON_AddNumberToObject(diag, "interlock_count", interlocks);
        cJSON_AddNumberToObject(diag, "uv_cut_total", uv_total);

        cJSON *history = cJSON_AddArrayToObject(diag, "uv_cut_events");
        if (history) {
            size_t stored = uv_total;
            if (stored > DOME_DIAG_UV_HISTORY_DEPTH) {
                stored = DOME_DIAG_UV_HISTORY_DEPTH;
            }
            uint8_t start = (uint8_t)((uv_head + DOME_DIAG_UV_HISTORY_DEPTH - stored) % DOME_DIAG_UV_HISTORY_DEPTH);
            for (size_t i = 0; i < stored; ++i) {
                uint8_t idx = (uint8_t)((start + i) % DOME_DIAG_UV_HISTORY_DEPTH);
                size_t base = (size_t)(DOME_REG_DIAG_UV_HISTORY - DOME_REG_BLOCK_DIAG + idx * DOME_DIAG_UV_EVENT_STRIDE);
                uint32_t encoded = (uint32_t)diag_buf[base] |
                                   ((uint32_t)diag_buf[base + 1] << 8) |
                                   ((uint32_t)diag_buf[base + 2] << 16) |
                                   ((uint32_t)diag_buf[base + 3] << 24);
                uint32_t ts = encoded & DOME_DIAG_UV_EVENT_TIMESTAMP_MASK;
                cJSON *entry = cJSON_CreateObject();
                if (!entry) {
                    continue;
                }
                cJSON_AddNumberToObject(entry, "timestamp_s", (double)ts);
                cJSON *channels = cJSON_AddArrayToObject(entry, "channels");
                if (channels) {
                    if (encoded & DOME_DIAG_UV_EVENT_CH_UVA) {
                        cJSON_AddItemToArray(channels, cJSON_CreateString("uva"));
                    }
                    if (encoded & DOME_DIAG_UV_EVENT_CH_UVB) {
                        cJSON_AddItemToArray(channels, cJSON_CreateString("uvb"));
                    }
                    if (cJSON_GetArraySize(channels) == 0) {
                        cJSON_AddItemToArray(channels, cJSON_CreateString("unknown"));
                    }
                }
                cJSON_AddItemToArray(history, entry);
            }
        }
    }

    cJSON *env = cJSON_GetObjectItem(root, "env");
    if (cJSON_IsObject(env)) {
        cJSON *env_uvi = cJSON_GetObjectItem(env, "uvi");
        if (cJSON_IsNumber(env_uvi)) {
            cJSON_SetNumberValue(env_uvi, uvi);
        } else {
            cJSON_AddNumberToObject(env, "uvi", uvi);
        }
        cJSON *env_irr = cJSON_GetObjectItem(env, "irradiance_uW_cm2");
        if (cJSON_IsNumber(env_irr)) {
            cJSON_SetNumberValue(env_irr, irradiance);
        } else {
            cJSON_AddNumberToObject(env, "irradiance_uW_cm2", irradiance);
        }
    }

    char summary[196];
    if (status & ST_UVI_FAULT) {
        snprintf(summary, sizeof(summary), "Status 0x%02X – Heatsink %.1f°C – UVI sensor fault", status, (float)heat);
    } else {
        snprintf(summary, sizeof(summary), "Status 0x%02X – Heatsink %.1f°C – UVI %.2f (%.1f µW/cm²)", status, (float)heat, uvi, irradiance);
    }
    cJSON_AddStringToObject(root, "summary", summary);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, ROOT_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    terra_sensors_t sensors = {0};
    uint32_t fault_mask = sensors_read(&sensors);
    float temp = NAN;
    float hum = NAN;
    if (sensors.temp_filtered_valid) {
        temp = sensors.temp_filtered_c;
    } else if (sensors.sht31_present) {
        temp = sensors.sht31_t_c;
    } else if (sensors.bme_present) {
        temp = sensors.bme_t_c;
    } else if (sensors.sht21_present) {
        temp = sensors.sht21_t_c;
    } else if (sensors.t1_present) {
        temp = sensors.t1_c;
    } else if (sensors.t2_present) {
        temp = sensors.t2_c;
    }

    if (sensors.humidity_filtered_valid) {
        hum = sensors.humidity_filtered_pct;
    } else if (sensors.sht31_present) {
        hum = sensors.sht31_rh;
    } else if (sensors.bme_present) {
        hum = sensors.bme_rh;
    } else if (sensors.sht21_present) {
        hum = sensors.sht21_rh;
    }
    cJSON *env = cJSON_AddObjectToObject(root, "env");
    if (isfinite(temp)) {
        cJSON_AddNumberToObject(env, "temperature", temp);
    }
    if (isfinite(hum)) {
        cJSON_AddNumberToObject(env, "humidity", hum);
    }
    if (sensors.temp_filtered_valid) {
        cJSON_AddNumberToObject(env, "temperature_filtered", sensors.temp_filtered_c);
    }
    if (sensors.humidity_filtered_valid) {
        cJSON_AddNumberToObject(env, "humidity_filtered", sensors.humidity_filtered_pct);
    }
    if (sensors.bme_present && isfinite(sensors.bme_p_hpa)) {
        cJSON_AddNumberToObject(env, "pressure", sensors.bme_p_hpa);
    }
    cJSON_AddNumberToObject(env, "sensor_fault_mask", (double)fault_mask);

    cJSON *sensor_status = cJSON_AddArrayToObject(root, "sensor_status");
    if (sensor_status) {
        for (size_t i = 0; i < TERRA_SENSOR_COUNT; ++i) {
            cJSON *entry = cJSON_CreateObject();
            if (!entry) {
                continue;
            }
            const terra_sensor_status_t *st = &sensors.status[i];
            cJSON_AddStringToObject(entry, "id", terra_sensor_names[i]);
            cJSON_AddBoolToObject(entry, "present", st->present);
            cJSON_AddBoolToObject(entry, "error", st->error);
            if (st->last_valid_timestamp_ms > 0) {
                cJSON_AddNumberToObject(entry, "last_valid_ms", (double)st->last_valid_timestamp_ms);
            }
            if (st->last_error != ESP_OK) {
                cJSON_AddStringToObject(entry, "last_error", esp_err_to_name(st->last_error));
            }
            cJSON_AddItemToArray(sensor_status, entry);
        }
    }

    if (fault_mask != 0) {
        char mask_buf[12];
        snprintf(mask_buf, sizeof(mask_buf), "0x%08" PRIX32, fault_mask);
        cJSON_AddStringToObject(root, "sensor_fault_mask_hex", mask_buf);
    }

    cJSON *alarms = cJSON_AddObjectToObject(root, "alarms");
    bool muted = alarms_get_mute();
    cJSON_AddBoolToObject(alarms, "muted", muted);

    float k = 0.0f, uvi_max = 0.0f;
    calib_get_uvb(&k, &uvi_max);
    cJSON *cal = cJSON_AddObjectToObject(root, "calibration");
    cJSON_AddNumberToObject(cal, "k", k);
    cJSON_AddNumberToObject(cal, "uvi_max", uvi_max);
    cJSON_AddNumberToObject(cal, "last_duty_pm", k > 0 ? uvi_max / k : 0);
    cJSON_AddNumberToObject(cal, "last_uvi", uvi_max);

    read_dome_status(root);

    climate_state_t state = {0};
    if (climate_get_state(&state)) {
        cJSON *cl = cJSON_AddObjectToObject(root, "climate");
        cJSON_AddBoolToObject(cl, "is_day", state.is_day);
        cJSON_AddNumberToObject(cl, "temp_setpoint", state.temp_setpoint_c);
        cJSON_AddNumberToObject(cl, "humidity_setpoint", state.humidity_setpoint_pct);
        cJSON_AddNumberToObject(cl, "uvi_target", state.uvi_target);
        cJSON_AddBoolToObject(cl, "heater_on", state.heater_on);
        cJSON_AddBoolToObject(cl, "lights_on", state.lights_on);
        cJSON_AddBoolToObject(cl, "fail_safe_active", !isfinite(state.temp_error_c));
        cJSON_AddBoolToObject(cl, "uvi_valid", state.uvi_valid);
        cJSON_AddNumberToObject(cl, "uvi_measured", state.uvi_valid ? state.uvi_measured : 0.0f);
        cJSON_AddNumberToObject(cl, "uvi_error", state.uvi_valid ? state.uvi_error : 0.0f);
        cJSON_AddNumberToObject(cl, "irradiance_uW_cm2", state.uvi_valid ? state.irradiance_uW_cm2 : 0.0f);
        if (!isfinite(state.temp_error_c)) {
            cJSON *summary = cJSON_GetObjectItem(root, "summary");
            if (cJSON_IsString(summary)) {
                char extended[196];
                snprintf(extended, sizeof(extended), "%s – fail-safe actif (capteurs T)", summary->valuestring);
                cJSON_ReplaceItemInObject(root, "summary", cJSON_CreateString(extended));
            }
        } else if (state.uvi_valid) {
            cJSON *summary = cJSON_GetObjectItem(root, "summary");
            if (cJSON_IsString(summary)) {
                char extended[220];
                snprintf(extended, sizeof(extended), "%s – ΔUVI %+0.2f", summary->valuestring, state.uvi_error);
                cJSON_ReplaceItemInObject(root, "summary", cJSON_CreateString(extended));
            }
        }
    }

    ota_state_append_status_json(root);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t api_light_get(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    if (read_dome_status(root) != ESP_OK) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "dome read failed");
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t api_light_post(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ESP_FAIL;
    }
    buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    light_payload_t payload = {0};
    char err_field[32] = {0};
    char err_detail[32] = {0};
    esp_err_t parse_err = light_payload_parse(root, &payload, err_field, sizeof(err_field), err_detail, sizeof(err_detail));
    if (parse_err != ESP_OK) {
        char msg[96];
        if (err_field[0] != '\0') {
            if (err_detail[0] != '\0') {
                snprintf(msg, sizeof(msg), "%s %s", err_detail, err_field);
            } else {
                snprintf(msg, sizeof(msg), "invalid field %s", err_field);
            }
        } else {
            snprintf(msg, sizeof(msg), "%s", "invalid payload");
        }
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
    }

    uint8_t cct_buf[4];
    wr16_le(&cct_buf[0], payload.cct_day);
    wr16_le(&cct_buf[2], payload.cct_warm);
    ESP_ERROR_CHECK_WITHOUT_ABORT(dome_bus_write(DOME_REG_BLOCK_CCT, cct_buf, sizeof(cct_buf)));

    uint8_t uva_buf[4];
    wr16_le(&uva_buf[0], payload.uva_set);
    wr16_le(&uva_buf[2], payload.uva_clamp);
    ESP_ERROR_CHECK_WITHOUT_ABORT(dome_bus_write(DOME_REG_BLOCK_UVA, uva_buf, sizeof(uva_buf)));

    float uvb_set = payload.uvb_set;
    float uvb_clamp = payload.uvb_clamp;
    float period = (float)payload.uvb_period;
    float duty_req = payload.uvb_duty;
    float k = 0.0f, uvi_max = 0.0f;
    calib_get_uvb(&k, &uvi_max);
    if (k > 0 && uvi_max > 0) {
        float allowed = uvi_max / k;
        if (uvb_set > allowed) {
            uvb_set = allowed;
        }
        if (duty_req > allowed) {
            duty_req = allowed;
        }
    }
    uint8_t uvb_buf[3];
    uvb_buf[0] = (uint8_t)period;
    uvb_buf[1] = reg_from_permille(duty_req);
    uvb_buf[2] = reg_from_permille(uvb_clamp);
    ESP_ERROR_CHECK_WITHOUT_ABORT(dome_bus_write(DOME_REG_BLOCK_UVB, uvb_buf, sizeof(uvb_buf)));

    if (payload.has_sky) {
        uint8_t sky_val = payload.sky_value;
        ESP_ERROR_CHECK_WITHOUT_ABORT(dome_bus_write(DOME_REG_SKY_CFG, &sky_val, 1));
    }

    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t api_diag_reset(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    uint8_t cmd = DOME_DIAG_CMD_RESET;
    esp_err_t err = dome_bus_write(DOME_REG_DIAG_CMD, &cmd, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dome diag reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reset failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

static esp_err_t api_calibration_get(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    float k = 0.0f, uvi_max = 0.0f;
    calib_get_uvb(&k, &uvi_max);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "k", k);
    cJSON_AddNumberToObject(root, "uvi_max", uvi_max);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t api_calibration_post(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ESP_FAIL;
    }
    buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    double duty = cJSON_GetObjectItem(root, "duty_pm")->valuedouble;
    double uvi = cJSON_GetObjectItem(root, "uvi")->valuedouble;
    double uvi_max = cJSON_GetObjectItem(root, "uvi_max")->valuedouble;
    if (uvi > 0 && duty > 0) {
        calib_set_uvb(duty, uvi);
    }
    if (uvi_max > 0) {
        calib_set_uvb_uvi_max(uvi_max);
    }
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t api_alarms_mute(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    bool muted = alarms_get_mute();
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"muted\":%s}", muted ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t api_alarms_toggle(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ESP_FAIL;
    }
    buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    cJSON *toggle = cJSON_GetObjectItem(root, "toggle");
    if (toggle && cJSON_IsTrue(toggle)) {
        alarms_set_mute(!alarms_get_mute());
    }
    bool muted = alarms_get_mute();
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    char out[64];
    snprintf(out, sizeof(out), "{\"muted\":%s}", muted ? "true" : "false");
    return httpd_resp_sendstr(req, out);
}

static cJSON *schedule_to_json(const climate_schedule_t *schedule)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "day_start_minute", schedule->day_start_minute);
    cJSON_AddNumberToObject(root, "night_start_minute", schedule->night_start_minute);
    cJSON *day = cJSON_AddObjectToObject(root, "day");
    cJSON_AddNumberToObject(day, "temp_c", schedule->day.temp_c);
    cJSON_AddNumberToObject(day, "humidity_pct", schedule->day.humidity_pct);
    cJSON_AddNumberToObject(day, "temp_hysteresis_c", schedule->day.temp_hysteresis_c);
    cJSON_AddNumberToObject(day, "humidity_hysteresis_pct", schedule->day.humidity_hysteresis_pct);
    cJSON_AddNumberToObject(day, "uvi_max", schedule->day_uvi_max);
    cJSON *night = cJSON_AddObjectToObject(root, "night");
    cJSON_AddNumberToObject(night, "temp_c", schedule->night.temp_c);
    cJSON_AddNumberToObject(night, "humidity_pct", schedule->night.humidity_pct);
    cJSON_AddNumberToObject(night, "temp_hysteresis_c", schedule->night.temp_hysteresis_c);
    cJSON_AddNumberToObject(night, "humidity_hysteresis_pct", schedule->night.humidity_hysteresis_pct);
    cJSON_AddNumberToObject(night, "uvi_max", schedule->night_uvi_max);
    return root;
}

static void add_metadata_json(cJSON *parent, const species_profile_metadata_t *meta)
{
    if (!parent) {
        return;
    }
    cJSON *meta_obj = cJSON_AddObjectToObject(parent, "metadata");
    if (!meta_obj || !meta) {
        return;
    }
    if (meta->habitat) {
        cJSON_AddStringToObject(meta_obj, "habitat", meta->habitat);
    }
    if (meta->uv_index_category) {
        cJSON_AddStringToObject(meta_obj, "uv_index_category", meta->uv_index_category);
    }
    if (meta->season_cycle) {
        cJSON_AddStringToObject(meta_obj, "season_cycle", meta->season_cycle);
    }
    cJSON_AddNumberToObject(meta_obj, "uv_index_peak", meta->uv_index_peak);
}

static void add_custom_metadata_json(cJSON *parent, const species_custom_profile_t *profile)
{
    if (!parent || !profile) {
        return;
    }
    cJSON *meta_obj = cJSON_AddObjectToObject(parent, "metadata");
    if (!meta_obj) {
        return;
    }
    if (profile->habitat[0] != '\0') {
        cJSON_AddStringToObject(meta_obj, "habitat", profile->habitat);
    }
    if (profile->uv_index_category[0] != '\0') {
        cJSON_AddStringToObject(meta_obj, "uv_index_category", profile->uv_index_category);
    }
    if (profile->season_cycle[0] != '\0') {
        cJSON_AddStringToObject(meta_obj, "season_cycle", profile->season_cycle);
    }
    cJSON_AddNumberToObject(meta_obj, "uv_index_peak", profile->uv_index_peak);
}

static esp_err_t api_species_get(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    ESP_RETURN_ON_ERROR(species_profiles_init(), TAG, "species init");

    const size_t total_builtin = species_profiles_builtin_count();
    const size_t total_custom = species_profiles_custom_count();
    const size_t max_locales = 24;
    char locale_codes[24][6] = {{0}};
    size_t locale_count = 0;
    for (size_t i = 0; i < total_builtin; ++i) {
        const species_profile_t *profile = species_profiles_builtin(i);
        if (!profile || !profile->labels) {
            continue;
        }
        for (size_t l = 0; l < profile->label_count; ++l) {
            const char *code = profile->labels[l].code;
            if (!code || code[0] == '\0') {
                continue;
            }
            bool exists = false;
            for (size_t idx = 0; idx < locale_count; ++idx) {
                if (strcasecmp(locale_codes[idx], code) == 0) {
                    exists = true;
                    break;
                }
            }
            if (!exists && locale_count < max_locales) {
                strlcpy(locale_codes[locale_count++], code, sizeof(locale_codes[0]));
            }
        }
    }
    const char *default_locales[] = { "fr", "en" };
    for (size_t d = 0; d < sizeof(default_locales) / sizeof(default_locales[0]); ++d) {
        bool exists = false;
        for (size_t idx = 0; idx < locale_count; ++idx) {
            if (strcasecmp(locale_codes[idx], default_locales[d]) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists && locale_count < max_locales) {
            strlcpy(locale_codes[locale_count++], default_locales[d], sizeof(locale_codes[0]));
        }
    }
    size_t builtin_page = 0;
    size_t builtin_per_page = total_builtin;
    size_t custom_page = 0;
    size_t custom_per_page = total_custom;
    const size_t max_page_size = 16;

    int query_len = httpd_req_get_url_query_len(req);
    char *query = NULL;
    if (query_len > 0) {
        query = malloc(query_len + 1);
        if (!query) {
            return ESP_ERR_NO_MEM;
        }
        if (httpd_req_get_url_query_str(req, query, query_len + 1) == ESP_OK) {
            char buf[16];
            if (httpd_query_key_value(query, "builtin_page", buf, sizeof(buf)) == ESP_OK) {
                builtin_page = strtoul(buf, NULL, 10);
            }
            if (httpd_query_key_value(query, "builtin_per_page", buf, sizeof(buf)) == ESP_OK) {
                builtin_per_page = strtoul(buf, NULL, 10);
            }
            if (httpd_query_key_value(query, "custom_page", buf, sizeof(buf)) == ESP_OK) {
                custom_page = strtoul(buf, NULL, 10);
            }
            if (httpd_query_key_value(query, "custom_per_page", buf, sizeof(buf)) == ESP_OK) {
                custom_per_page = strtoul(buf, NULL, 10);
            }
        }
        free(query);
    }

    if (builtin_per_page == 0 || builtin_per_page > max_page_size) {
        builtin_per_page = max_page_size;
    }
    if (custom_per_page == 0 || custom_per_page > max_page_size) {
        custom_per_page = max_page_size;
    }

    size_t builtin_offset = builtin_page * builtin_per_page;
    if (builtin_offset > total_builtin) {
        builtin_offset = total_builtin;
    }
    size_t builtin_end = builtin_offset + builtin_per_page;
    if (builtin_end > total_builtin) {
        builtin_end = total_builtin;
    }

    size_t custom_offset = custom_page * custom_per_page;
    if (custom_offset > total_custom) {
        custom_offset = total_custom;
    }
    size_t custom_end = custom_offset + custom_per_page;
    if (custom_end > total_custom) {
        custom_end = total_custom;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    char active[48] = {0};
    if (species_profiles_get_active_key(active, sizeof(active)) == ESP_OK) {
        cJSON_AddStringToObject(root, "active_key", active);
    }

    cJSON *locale_arr = cJSON_AddArrayToObject(root, "locales");
    if (locale_arr) {
        for (size_t i = 0; i < locale_count; ++i) {
            cJSON_AddItemToArray(locale_arr, cJSON_CreateString(locale_codes[i]));
        }
    }

    cJSON *builtin_obj = cJSON_AddObjectToObject(root, "builtin");
    cJSON *builtin_items = cJSON_AddArrayToObject(builtin_obj, "items");
    cJSON_AddNumberToObject(builtin_obj, "total", (double)total_builtin);
    cJSON_AddNumberToObject(builtin_obj, "page", (double)builtin_page);
    cJSON_AddNumberToObject(builtin_obj, "per_page", (double)builtin_per_page);

    for (size_t i = builtin_offset; i < builtin_end; ++i) {
        const species_profile_t *profile = species_profiles_builtin(i);
        if (!profile) {
            continue;
        }
        cJSON *entry = cJSON_CreateObject();
        if (!entry) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(entry, "key", profile->key);
        cJSON *labels = cJSON_AddObjectToObject(entry, "labels");
        if (labels) {
            for (size_t l = 0; l < profile->label_count; ++l) {
                cJSON_AddStringToObject(labels, profile->labels[l].code, profile->labels[l].label);
            }
        }
        add_metadata_json(entry, &profile->metadata);
        cJSON *sched = schedule_to_json(&profile->schedule);
        if (sched) {
            cJSON_AddItemToObject(entry, "schedule", sched);
        }
        cJSON_AddItemToArray(builtin_items, entry);
    }

    cJSON *custom_obj = cJSON_AddObjectToObject(root, "custom");
    cJSON *custom_items = cJSON_AddArrayToObject(custom_obj, "items");
    cJSON_AddNumberToObject(custom_obj, "total", (double)total_custom);
    cJSON_AddNumberToObject(custom_obj, "page", (double)custom_page);
    cJSON_AddNumberToObject(custom_obj, "per_page", (double)custom_per_page);

    for (size_t i = custom_offset; i < custom_end; ++i) {
        species_custom_profile_t profile = {0};
        if (species_profiles_custom_get(i, &profile) != ESP_OK) {
            continue;
        }
        cJSON *entry = cJSON_CreateObject();
        if (!entry) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(entry, "key", profile.key);
        cJSON_AddStringToObject(entry, "name", profile.name);
        add_custom_metadata_json(entry, &profile);
        cJSON_AddNumberToObject(entry, "uv_index_peak", profile.uv_index_peak);
        cJSON *sched = schedule_to_json(&profile.schedule);
        if (sched) {
            cJSON_AddItemToObject(entry, "schedule", sched);
        }
        cJSON_AddItemToArray(custom_items, entry);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t api_species_apply(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ESP_FAIL;
    }
    buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(root, "key"));
    if (!key) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing key");
    }
    esp_err_t err = species_profiles_apply(key);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown profile");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t api_species_custom(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ESP_FAIL;
    }
    buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
    cJSON *schedule_obj = cJSON_GetObjectItem(root, "schedule");
    if (!name || !schedule_obj) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
    }
    climate_schedule_t schedule = {0};
    schedule.day_start_minute = cJSON_GetObjectItem(schedule_obj, "day_start_minute")->valueint;
    schedule.night_start_minute = cJSON_GetObjectItem(schedule_obj, "night_start_minute")->valueint;
    cJSON *day = cJSON_GetObjectItem(schedule_obj, "day");
    cJSON *night = cJSON_GetObjectItem(schedule_obj, "night");
    schedule.day.temp_c = cJSON_GetObjectItem(day, "temp_c")->valuedouble;
    schedule.day.humidity_pct = cJSON_GetObjectItem(day, "humidity_pct")->valuedouble;
    schedule.day.temp_hysteresis_c = cJSON_GetObjectItem(day, "temp_hysteresis_c")->valuedouble;
    schedule.day.humidity_hysteresis_pct = cJSON_GetObjectItem(day, "humidity_hysteresis_pct")->valuedouble;
    schedule.day_uvi_max = cJSON_GetObjectItem(day, "uvi_max")->valuedouble;
    schedule.night.temp_c = cJSON_GetObjectItem(night, "temp_c")->valuedouble;
    schedule.night.humidity_pct = cJSON_GetObjectItem(night, "humidity_pct")->valuedouble;
    schedule.night.temp_hysteresis_c = cJSON_GetObjectItem(night, "temp_hysteresis_c")->valuedouble;
    schedule.night.humidity_hysteresis_pct = cJSON_GetObjectItem(night, "humidity_hysteresis_pct")->valuedouble;
    schedule.night_uvi_max = cJSON_GetObjectItem(night, "uvi_max")->valuedouble;

    char key[32];
    species_profile_metadata_t meta = {0};
    char habitat[128] = {0};
    char category[32] = {0};
    char season[48] = {0};
    bool meta_present = false;
    cJSON *metadata_obj = cJSON_GetObjectItem(root, "metadata");
    if (cJSON_IsObject(metadata_obj)) {
        cJSON *hab = cJSON_GetObjectItem(metadata_obj, "habitat");
        if (cJSON_IsString(hab) && hab->valuestring) {
            strlcpy(habitat, hab->valuestring, sizeof(habitat));
            meta_present = true;
        }
        cJSON *cat = cJSON_GetObjectItem(metadata_obj, "uv_index_category");
        if (cJSON_IsString(cat) && cat->valuestring) {
            strlcpy(category, cat->valuestring, sizeof(category));
            meta_present = true;
        }
        cJSON *season_obj = cJSON_GetObjectItem(metadata_obj, "season_cycle");
        if (cJSON_IsString(season_obj) && season_obj->valuestring) {
            strlcpy(season, season_obj->valuestring, sizeof(season));
            meta_present = true;
        }
        cJSON *uvp = cJSON_GetObjectItem(metadata_obj, "uv_index_peak");
        if (cJSON_IsNumber(uvp)) {
            meta.uv_index_peak = uvp->valuedouble;
            meta_present = true;
        }
    }
    meta.habitat = habitat[0] ? habitat : NULL;
    meta.uv_index_category = category[0] ? category : NULL;
    meta.season_cycle = season[0] ? season : NULL;
    const species_profile_metadata_t *meta_ptr = meta_present ? &meta : NULL;

    esp_err_t err = species_profiles_save_custom(name, &schedule, meta_ptr, key, sizeof(key));
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    httpd_resp_set_type(req, "application/json");
    char out[128];
    snprintf(out, sizeof(out), "{\"key\":\"%s\"}", key);
    return httpd_resp_sendstr(req, out);
}

static esp_err_t api_security_rotate(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    bool rotate_cert = true;
    bool rotate_token = true;
    int remaining = req->content_len;
    char payload[128] = {0};
    int offset = 0;
    while (remaining > 0 && offset < (int)(sizeof(payload) - 1)) {
        size_t to_read = remaining;
        size_t capacity = sizeof(payload) - 1 - offset;
        if (to_read > capacity) {
            to_read = capacity;
        }
        int chunk = httpd_req_recv(req, payload + offset, to_read);
        if (chunk <= 0) {
            break;
        }
        offset += chunk;
        remaining -= chunk;
    }
    while (remaining > 0) {
        char discard[32];
        size_t to_read = remaining > (int)sizeof(discard) ? sizeof(discard) : remaining;
        int chunk = httpd_req_recv(req, discard, to_read);
        if (chunk <= 0) {
            break;
        }
        remaining -= chunk;
    }
    payload[offset] = '\0';
    if (offset > 0) {
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            cJSON *cert = cJSON_GetObjectItem(root, "rotate_cert");
            if (cJSON_IsBool(cert)) {
                rotate_cert = cJSON_IsTrue(cert);
            }
            cJSON *token = cJSON_GetObjectItem(root, "rotate_token");
            if (cJSON_IsBool(token)) {
                rotate_token = cJSON_IsTrue(token);
            }
            cJSON_Delete(root);
        }
    }
    esp_err_t err = credentials_rotate(rotate_cert, rotate_token);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rotation failed");
    }
    const char *new_token = rotate_token ? credentials_bootstrap_token() : NULL;
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(resp, "rotate_cert", rotate_cert);
    cJSON_AddBoolToObject(resp, "rotate_token", rotate_token);
    if (rotate_token) {
        if (new_token) {
            cJSON_AddStringToObject(resp, "token", new_token);
        } else {
            cJSON_AddNullToObject(resp, "token");
        }
    }
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_sendstr(req, json);
    free(json);
    return send_err;
}

static esp_err_t api_species_export(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    uint8_t nonce[16];
    uint8_t signature[32];
    esp_err_t err = species_profiles_export_secure(&blob, &blob_len, nonce, signature);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "export failed");
    }

    size_t payload_len = 0;
    if (mbedtls_base64_encode(NULL, 0, &payload_len, blob, blob_len) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        payload_len = ((blob_len + 2) / 3) * 4;
    }
    char *payload_b64 = (char *)malloc(payload_len + 1);
    if (!payload_b64) {
        free(blob);
        return ESP_ERR_NO_MEM;
    }
    if (mbedtls_base64_encode((unsigned char *)payload_b64, payload_len, &payload_len, blob, blob_len) != 0) {
        free(payload_b64);
        free(blob);
        return ESP_ERR_INVALID_STATE;
    }
    payload_b64[payload_len] = '\0';

    size_t nonce_len = 0;
    if (mbedtls_base64_encode(NULL, 0, &nonce_len, nonce, sizeof(nonce)) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        nonce_len = ((sizeof(nonce) + 2) / 3) * 4;
    }
    char *nonce_b64 = (char *)malloc(nonce_len + 1);
    if (!nonce_b64) {
        free(payload_b64);
        free(blob);
        return ESP_ERR_NO_MEM;
    }
    if (mbedtls_base64_encode((unsigned char *)nonce_b64, nonce_len, &nonce_len, nonce, sizeof(nonce)) != 0) {
        free(nonce_b64);
        free(payload_b64);
        free(blob);
        return ESP_ERR_INVALID_STATE;
    }
    nonce_b64[nonce_len] = '\0';

    size_t sig_len = 0;
    if (mbedtls_base64_encode(NULL, 0, &sig_len, signature, sizeof(signature)) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        sig_len = ((sizeof(signature) + 2) / 3) * 4;
    }
    char *signature_b64 = (char *)malloc(sig_len + 1);
    if (!signature_b64) {
        free(nonce_b64);
        free(payload_b64);
        free(blob);
        return ESP_ERR_NO_MEM;
    }
    if (mbedtls_base64_encode((unsigned char *)signature_b64, sig_len, &sig_len, signature, sizeof(signature)) != 0) {
        free(signature_b64);
        free(nonce_b64);
        free(payload_b64);
        free(blob);
        return ESP_ERR_INVALID_STATE;
    }
    signature_b64[sig_len] = '\0';

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(signature_b64);
        free(nonce_b64);
        free(payload_b64);
        free(blob);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", CUSTOM_BLOB_VERSION);
    cJSON_AddStringToObject(root, "algorithm", "HMAC-SHA256");
    cJSON_AddStringToObject(root, "payload", payload_b64);
    cJSON_AddStringToObject(root, "nonce", nonce_b64);
    cJSON_AddStringToObject(root, "signature", signature_b64);

    free(signature_b64);
    free(nonce_b64);
    free(payload_b64);
    free(blob);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_sendstr(req, json);
    free(json);
    return resp;
}

static esp_err_t api_species_import(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    if (req->content_len <= 0 || req->content_len > 4096) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid size");
    }
    char *body = (char *)malloc(req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        free(body);
        return ESP_FAIL;
    }
    body[received] = '\0';
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    const char *payload_b64 = cJSON_GetStringValue(cJSON_GetObjectItem(root, "payload"));
    const char *nonce_b64 = cJSON_GetStringValue(cJSON_GetObjectItem(root, "nonce"));
    const char *signature_b64 = cJSON_GetStringValue(cJSON_GetObjectItem(root, "signature"));
    if (!payload_b64 || !nonce_b64 || !signature_b64) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
    }

    size_t payload_len = strlen(payload_b64);
    size_t decoded_payload_len = (payload_len / 4) * 3 + 1;
    uint8_t *payload = (uint8_t *)malloc(decoded_payload_len);
    if (!payload) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    if (mbedtls_base64_decode(payload, decoded_payload_len, &decoded_payload_len, (const unsigned char *)payload_b64, payload_len) != 0) {
        free(payload);
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "payload decode failed");
    }

    size_t nonce_len = strlen(nonce_b64);
    uint8_t decoded_nonce[16];
    size_t decoded_nonce_len = sizeof(decoded_nonce);
    if (mbedtls_base64_decode(decoded_nonce, sizeof(decoded_nonce), &decoded_nonce_len, (const unsigned char *)nonce_b64, nonce_len) != 0 || decoded_nonce_len != sizeof(decoded_nonce)) {
        free(payload);
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "nonce decode failed");
    }

    size_t sig_len = strlen(signature_b64);
    uint8_t decoded_signature[32];
    size_t decoded_signature_len = sizeof(decoded_signature);
    if (mbedtls_base64_decode(decoded_signature, sizeof(decoded_signature), &decoded_signature_len, (const unsigned char *)signature_b64, sig_len) != 0 || decoded_signature_len != sizeof(decoded_signature)) {
        free(payload);
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "signature decode failed");
    }

    esp_err_t err = species_profiles_import_secure(payload, decoded_payload_len, decoded_nonce, decoded_signature);
    free(payload);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "import failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t handle_ota_controller(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);

    ota_manifest_t manifest = {0};
    esp_err_t err = read_manifest_header(req, &manifest);
    if (err != ESP_OK) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Manifest invalide");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid manifest");
    }
    if (!ota_manifest_is_target(&manifest, OTA_TARGET_CONTROLLER)) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Cible manifest erronée");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "manifest target mismatch");
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Partition courante introuvable");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no running partition");
    }
    esp_app_desc_t running_desc = {0};
    ESP_RETURN_ON_ERROR(esp_ota_get_partition_description(running, &running_desc), TAG, "desc courant");

    if (ota_manifest_compare_versions(running_desc.version, manifest.version) >= 0) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Version non monotone");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "firmware version not newer");
    }

    esp_err_t state_err = ota_state_begin(OTA_TARGET_CONTROLLER, &manifest, "Manifest validé");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_begin failed: %s", esp_err_to_name(state_err));
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Partition OTA indisponible");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota partition");
    }

    esp_ota_handle_t handle = 0;
    err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "esp_ota_begin échec");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    state_err = ota_state_transition(OTA_TARGET_CONTROLLER, OTA_STATE_DOWNLOADING, "Réception en cours");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_transition failed: %s", esp_err_to_name(state_err));
    }

    int total = 0;
    int received = 0;
    uint8_t buf[1024];
    while ((received = httpd_req_recv(req, (char *)buf, sizeof(buf))) > 0) {
        mbedtls_sha256_update(&sha_ctx, buf, received);
        err = esp_ota_write(handle, buf, received);
        if (err != ESP_OK) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Écriture OTA échouée");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write failed");
            esp_ota_abort(handle);
            mbedtls_sha256_free(&sha_ctx);
            return ESP_ERR_HTTPD_RESP_SENT;
        }
        total += received;
    }
    if (received < 0) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Flux OTA interrompu");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota receive failed");
        esp_ota_abort(handle);
        mbedtls_sha256_free(&sha_ctx);
        return ESP_ERR_HTTPD_RESP_SENT;
    }

    if (manifest.image_size != 0 && manifest.image_size != (uint32_t)total) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Taille inattendue");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "size mismatch");
        esp_ota_abort(handle);
        mbedtls_sha256_free(&sha_ctx);
        return ESP_ERR_HTTPD_RESP_SENT;
    }

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha_ctx, digest);
    mbedtls_sha256_free(&sha_ctx);
    if (memcmp(digest, manifest.image_sha256, sizeof(digest)) != 0) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Hash SHA-256 invalide");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "sha256 mismatch");
        esp_ota_abort(handle);
        return ESP_ERR_HTTPD_RESP_SENT;
    }

    state_err = ota_state_transition(OTA_TARGET_CONTROLLER, OTA_STATE_VERIFYING, "Hash validé");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_transition failed: %s", esp_err_to_name(state_err));
    }

    err = esp_ota_end(handle);
    handle = 0;
    if (err != ESP_OK) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "esp_ota_end échec");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota end failed");
    }

    esp_app_desc_t new_desc = {0};
    ESP_RETURN_ON_ERROR(esp_ota_get_image_desc(partition, &new_desc), TAG, "desc nouvelle image");

    if (strncmp(new_desc.version, manifest.version, sizeof(new_desc.version)) != 0) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Version manifest ≠ binaire");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "version mismatch");
    }

    if (ota_manifest_compare_versions(running_desc.version, new_desc.version) >= 0) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Version non monotone (binaire)");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image version not newer");
    }

    state_err = ota_state_transition(OTA_TARGET_CONTROLLER, OTA_STATE_READY, "Basculement préparé");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_transition failed: %s", esp_err_to_name(state_err));
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ota_state_fail(OTA_TARGET_CONTROLLER, "Sélection partition échouée");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot failed");
    }

    state_err = ota_state_transition(OTA_TARGET_CONTROLLER, OTA_STATE_PENDING_REBOOT, "Redémarrage imminent");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_transition failed: %s", esp_err_to_name(state_err));
    }

    char size_buf[16];
    snprintf(size_buf, sizeof(size_buf), "%d", total);
    httpd_resp_set_hdr(req, "X-OTA-Size", size_buf);

    char sha_hex[65];
    ota_manifest_sha256_to_hex(digest, sha_hex);

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "bytes", total);
    cJSON_AddStringToObject(resp, "version", new_desc.version);
    cJSON_AddStringToObject(resp, "sha256", sha_hex);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    ESP_LOGI(TAG, "Controller OTA flashed %d bytes (v%s)", total, new_desc.version);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t dome_ota_chunk_cb(const uint8_t *chunk, size_t len, void *ctx)
{
    (void)ctx;
    if (len == 0) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(dome_bus_write(DOME_REG_BLOCK_OTA_DATA, chunk, len), TAG, "ota data");
    uint8_t write_cmd = DOME_OTA_CMD_WRITE;
    return dome_bus_write(DOME_REG_OTA_CMD, &write_cmd, 1);
}

static esp_err_t handle_ota_dome(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    ota_manifest_t manifest = {0};
    esp_err_t err = read_manifest_header(req, &manifest);
    if (err != ESP_OK) {
        ota_state_fail(OTA_TARGET_DOME, "Manifest invalide");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid manifest");
    }
    if (!ota_manifest_is_target(&manifest, OTA_TARGET_DOME)) {
        ota_state_fail(OTA_TARGET_DOME, "Cible manifest erronée");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "manifest target mismatch");
    }

    esp_err_t state_err = ota_state_begin(OTA_TARGET_DOME, &manifest, "Manifest validé");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_begin(dome) failed: %s", esp_err_to_name(state_err));
    }

    err = dome_stage_manifest(&manifest, "Préparation OTA");
    if (err != ESP_OK) {
        ota_state_fail(OTA_TARGET_DOME, "Chargement métadonnées échec");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "dome meta failed");
    }

    state_err = ota_state_transition(OTA_TARGET_DOME, OTA_STATE_DOWNLOADING, "Réception en cours");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_transition dome failed: %s", esp_err_to_name(state_err));
    }

    uint8_t cmd = DOME_OTA_CMD_BEGIN;
    ESP_RETURN_ON_ERROR(dome_bus_write(DOME_REG_OTA_CMD, &cmd, 1), TAG, "dome ota begin");

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    int received;
    uint8_t buf[256];
    int total = 0;
    while ((received = httpd_req_recv(req, (char *)buf, sizeof(buf))) > 0) {
        mbedtls_sha256_update(&sha_ctx, buf, received);
        err = ota_stream_chunks(buf, received, DOME_REG_BLOCK_OTA_DATA_LEN, dome_ota_chunk_cb, NULL);
        if (err != ESP_OK) {
            uint8_t abort_cmd = DOME_OTA_CMD_ABORT;
            dome_bus_write(DOME_REG_OTA_CMD, &abort_cmd, 1);
            ota_state_fail(OTA_TARGET_DOME, "Écriture I2C échouée");
            mbedtls_sha256_free(&sha_ctx);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota stream failed");
        }
        total += received;
    }
    if (received < 0) {
        uint8_t abort_cmd = DOME_OTA_CMD_ABORT;
        dome_bus_write(DOME_REG_OTA_CMD, &abort_cmd, 1);
        ota_state_fail(OTA_TARGET_DOME, "Flux OTA interrompu");
        mbedtls_sha256_free(&sha_ctx);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota receive failed");
    }

    if (manifest.image_size != 0 && manifest.image_size != (uint32_t)total) {
        uint8_t abort_cmd = DOME_OTA_CMD_ABORT;
        dome_bus_write(DOME_REG_OTA_CMD, &abort_cmd, 1);
        ota_state_fail(OTA_TARGET_DOME, "Taille inattendue");
        mbedtls_sha256_free(&sha_ctx);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "size mismatch");
    }

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha_ctx, digest);
    mbedtls_sha256_free(&sha_ctx);
    if (memcmp(digest, manifest.image_sha256, sizeof(digest)) != 0) {
        uint8_t abort_cmd = DOME_OTA_CMD_ABORT;
        dome_bus_write(DOME_REG_OTA_CMD, &abort_cmd, 1);
        ota_state_fail(OTA_TARGET_DOME, "Hash SHA-256 invalide");
        dome_write_status_message("Hash invalide");
        uint8_t flags = DOME_OTA_FLAG_META_READY | DOME_OTA_FLAG_HASH_FAIL;
        dome_bus_write(DOME_REG_OTA_FLAGS, &flags, 1);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "sha256 mismatch");
    }

    uint8_t flags = DOME_OTA_FLAG_META_READY | DOME_OTA_FLAG_HASH_OK;
    dome_bus_write(DOME_REG_OTA_FLAGS, &flags, 1);
    dome_write_status_message("Hash validé");

    state_err = ota_state_transition(OTA_TARGET_DOME, OTA_STATE_VERIFYING, "Hash validé");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_transition dome verify failed: %s", esp_err_to_name(state_err));
    }

    uint8_t commit = DOME_OTA_CMD_COMMIT;
    ESP_RETURN_ON_ERROR(dome_bus_write(DOME_REG_OTA_CMD, &commit, 1), TAG, "ota commit");

    state_err = ota_state_transition(OTA_TARGET_DOME, OTA_STATE_PENDING_REBOOT, "Commit envoyé");
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "ota_state_transition dome pending failed: %s", esp_err_to_name(state_err));
    }

    char size_buf[16];
    snprintf(size_buf, sizeof(size_buf), "%d", total);
    httpd_resp_set_hdr(req, "X-OTA-Size", size_buf);

    char sha_hex[65];
    ota_manifest_sha256_to_hex(digest, sha_hex);

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "bytes", total);
    cJSON_AddStringToObject(resp, "sha256", sha_hex);
    cJSON_AddStringToObject(resp, "version", manifest.version);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

void httpd_start_secure(void)
{
    if (s_server) {
        return;
    }
    if (credentials_init() != ESP_OK) {
        ESP_LOGE(TAG, "Unable to load TLS credentials");
        return;
    }
    size_t cert_len = 0;
    size_t key_len = 0;
    const uint8_t *cert = credentials_server_cert(&cert_len);
    const uint8_t *key = credentials_server_key(&key_len);
    if (!cert || !key || cert_len == 0 || key_len == 0) {
        ESP_LOGE(TAG, "TLS keypair unavailable");
        return;
    }
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.httpd.uri_match_fn = httpd_uri_match_wildcard;
    conf.port_secure = 443;
    conf.servercert = cert;
    conf.servercert_len = cert_len;
    conf.prvtkey_pem = key;
    conf.prvtkey_len = key_len;
    conf.httpd.max_uri_handlers = 20;

    esp_err_t err = httpd_ssl_start(&s_server, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "HTTPS server running on port %d", conf.port_secure);

    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/", .method = HTTP_GET, .handler = root_handler});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/maintenance/reset_diagnostics", .method = HTTP_POST, .handler = api_diag_reset});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/light/dome0", .method = HTTP_GET, .handler = api_light_get});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/light/dome0", .method = HTTP_POST, .handler = api_light_post});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/calibrate/uvb", .method = HTTP_GET, .handler = api_calibration_get});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/calibrate/uvb", .method = HTTP_POST, .handler = api_calibration_post});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/alarms/mute", .method = HTTP_GET, .handler = api_alarms_mute});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/alarms/mute", .method = HTTP_POST, .handler = api_alarms_toggle});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/species", .method = HTTP_GET, .handler = api_species_get});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/species/apply", .method = HTTP_POST, .handler = api_species_apply});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/species/custom", .method = HTTP_POST, .handler = api_species_custom});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/security/rotate", .method = HTTP_POST, .handler = api_security_rotate});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/ota/controller", .method = HTTP_POST, .handler = handle_ota_controller});
    httpd_register_uri_handler(s_server, &(httpd_uri_t){.uri = "/api/ota/dome", .method = HTTP_POST, .handler = handle_ota_dome});
}
