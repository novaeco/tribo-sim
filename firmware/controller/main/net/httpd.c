#include "httpd.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    "progress::-webkit-progress-value{background:#3a86ff;}details{margin-top:12px;}summary{cursor:pointer;font-weight:600;}"
    ".ota-block{margin-top:12px;padding:12px;border-radius:12px;background:rgba(0,0,0,0.18);border:1px solid rgba(255,255,255,0.08);}""
    " .ota-block h3{margin:0 0 8px;font-size:1.1rem;} .ota-status-line{font-size:0.85rem;margin-top:6px;color:rgba(255,255,255,0.8);}""
    " .ota-status-line span{display:block;margin-top:2px;word-break:break-all;}""</style></head><body>"
    "<h1>Terrarium S3</h1>"
    "<div id='statusBanner' class='ok'></div>"
    "<section><label for='languageSelect' data-i18n='language'></label><select id='languageSelect'><option value='fr'>Français</option><option value='en'>English</option><option value='es'>Español</option></select>"
    "<label for='speciesSelect' data-i18n='species_profile'></label><select id='speciesSelect'></select><button id='applySpecies' data-i18n='apply_profile'></button>"
    "<details><summary data-i18n='custom_profile'></summary><div><label data-i18n='profile_name'></label><input id='customName' placeholder='My species'>"
    "<textarea id='customSchedule' rows='8' style='width:100%;border-radius:10px;padding:10px;background:rgba(255,255,255,0.08);color:#fefefe;' data-i18n-placeholder='custom_schedule_hint'></textarea>"
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
    "<section><h2 data-i18n='alarms'></h2><button id='toggleMute'></button><div id='alarmState'></div></section>"
    "<section><h2 data-i18n='calibration'></h2><label data-i18n='uvi_max'></label><input id='calUviMax' type='number' step='0.1'><label data-i18n='cal_duty'></label><input id='calDuty' type='number'><label data-i18n='cal_measured'></label><input id='calMeasured' type='number' step='0.01'><button id='applyCalibration' data-i18n='apply_calibration'></button></section>"
    "<script>const I18N={fr:{language:'Langue',species_profile:'Profil d\'espèce',apply_profile:'Appliquer le profil',custom_profile:'Profil personnalisé',profile_name:'Nom du profil',save_custom:'Enregistrer',custom_schedule_hint:'JSON climate_schedule_t',light_control:'Contrôle lumineux',cct_day:'CCT Jour (‰)',cct_warm:'CCT Chaud (‰)',uva_set:'UVA consigne (‰)',uva_clamp:'UVA limite (‰)',uvb_set:'UVB consigne (‰)',uvb_clamp:'UVB limite (‰)',uvb_period:'Période UVB (s)',uvb_duty:'Duty UVB (‰)',sky_mode:'Mode ciel',apply_light:'Appliquer',telemetry:'Télémétries en temps réel',metric:'Mesure',value:'Valeur',ota_updates:'Mises à jour OTA',controller_title:'Contrôleur',controller_manifest:'Manifeste contrôleur (.json signé)',controller_fw:'Firmware contrôleur (.bin)',flash_controller:'Flasher contrôleur',dome_title:'Dôme',dome_manifest:'Manifeste dôme (.json signé)',dome_fw:'Firmware dôme (.bin)',flash_dome:'Flasher dôme',ota_status_label:'Statut OTA',manifest_required:'Manifeste requis',firmware_required:'Fichier firmware requis',alarms:'Alarmes',apply_calibration:'Enregistrer calibration',calibration:'Calibration UVB',uvi_max:'UVI cible',cal_duty:'Duty mesuré (‰)',cal_measured:'UVI mesuré',uvi_fault:'Capteur UVI en défaut'},en:{language:'Language',species_profile:'Species profile',apply_profile:'Apply profile',custom_profile:'Custom profile',profile_name:'Profile name',save_custom:'Save custom profile',custom_schedule_hint:'climate_schedule_t JSON payload',light_control:'Lighting control',cct_day:'CCT Day (‰)',cct_warm:'CCT Warm (‰)',uva_set:'UVA setpoint (‰)',uva_clamp:'UVA clamp (‰)',uvb_set:'UVB setpoint (‰)',uvb_clamp:'UVB clamp (‰)',uvb_period:'UVB period (s)',uvb_duty:'UVB duty (‰)',sky_mode:'Sky mode',apply_light:'Apply',telemetry:'Real-time telemetry',metric:'Metric',value:'Value',ota_updates:'OTA updates',controller_title:'Controller',controller_manifest:'Controller manifest (signed .json)',controller_fw:'Controller firmware (.bin)',flash_controller:'Flash controller',dome_title:'Dome',dome_manifest:'Dome manifest (signed .json)',dome_fw:'Dome firmware (.bin)',flash_dome:'Flash dome',ota_status_label:'OTA status',manifest_required:'Manifest required',firmware_required:'Firmware file required',alarms:'Alarms',apply_calibration:'Apply calibration',calibration:'UVB calibration',uvi_max:'Target UVI',cal_duty:'Duty measured (‰)',cal_measured:'Measured UVI',uvi_fault:'UVI sensor fault'},es:{language:'Idioma',species_profile:'Perfil de especie',apply_profile:'Aplicar perfil',custom_profile:'Perfil personalizado',profile_name:'Nombre del perfil',save_custom:'Guardar personalizado',custom_schedule_hint:'JSON climate_schedule_t',light_control:'Control lumínico',cct_day:'CCT Día (‰)',cct_warm:'CCT Cálido (‰)',uva_set:'UVA consigna (‰)',uva_clamp:'UVA límite (‰)',uvb_set:'UVB consigna (‰)',uvb_clamp:'UVB límite (‰)',uvb_period:'Periodo UVB (s)',uvb_duty:'Duty UVB (‰)',sky_mode:'Modo cielo',apply_light:'Aplicar',telemetry:'Telemetría en tiempo real',metric:'Métrica',value:'Valor',ota_updates:'Actualizaciones OTA',controller_title:'Controlador',controller_manifest:'Manifiesto controlador (.json firmado)',controller_fw:'Firmware controlador (.bin)',flash_controller:'Flashear controlador',dome_title:'Cúpula',dome_manifest:'Manifiesto cúpula (.json firmado)',dome_fw:'Firmware cúpula (.bin)',flash_dome:'Flashear cúpula',ota_status_label:'Estado OTA',manifest_required:'Manifiesto requerido',firmware_required:'Archivo de firmware requerido',alarms:'Alarmas',apply_calibration:'Guardar calibración',calibration:'Calibración UVB',uvi_max:'UVI objetivo',cal_duty:'Duty medido (‰)',cal_measured:'UVI medido',uvi_fault:'Sensor UVI en fallo'}};let lang='fr';const banner=document.getElementById('statusBanner');function setLang(l){lang=l;const dict=I18N[l]||I18N.fr;document.querySelectorAll('[data-i18n]').forEach(el=>{const k=el.getAttribute('data-i18n');if(dict[k])el.textContent=dict[k];});document.querySelectorAll('[data-i18n-placeholder]').forEach(el=>{const k=el.getAttribute('data-i18n-placeholder');if(dict[k])el.setAttribute('placeholder',dict[k]);});document.getElementById('toggleMute').textContent=dict.alarms+' – mute';}async function fetchJSON(url,opts){const r=await fetch(url,opts);if(!r.ok) throw new Error(await r.text());return r.json();}function permilleFromReg(v){return v*40;}function regFromPermille(p){return Math.min(255,Math.max(0,Math.round(p/40)));}function encodeManifest(text){return btoa(unescape(encodeURIComponent(text)));}function describeOta(entry){if(!entry)return'--';const parts=[];if(entry.version)parts.push(entry.version);if(entry.message)parts.push(entry.message);else if(entry.state)parts.push(entry.state);if(entry.sha256)parts.push(entry.sha256.slice(0,8)+'…');return parts.join(' • ');}function updateBanner(text,isErr){banner.textContent=text;banner.className=isErr?'error':'ok';}const chartCtx=document.getElementById('telemetryChart').getContext('2d');const chartState={points:[]};function renderChart(){const ctx=chartCtx;const {width,height}=ctx.canvas;ctx.clearRect(0,0,width,height);ctx.strokeStyle='#2dd4ff';ctx.lineWidth=2;ctx.beginPath();chartState.points.forEach((p,i)=>{const x=width*(i/(chartState.points.length-1||1));const y=height*(1-p.tempNorm);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();ctx.strokeStyle='#fbbf24';ctx.beginPath();chartState.points.forEach((p,i)=>{const x=width*(i/(chartState.points.length-1||1));const y=height*(1-p.humNorm);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();}async function refreshSpecies(){const data=await fetchJSON('/api/species');const select=document.getElementById('speciesSelect');select.innerHTML='';const addOpt=(key,label)=>{const o=document.createElement('option');o.value=key;o.textContent=label;select.appendChild(o);};data.builtin.forEach(p=>{const label=(I18N[lang]?I18N[lang].species_profile: 'Profile')+': '+(p.labels[lang]||p.labels.fr);addOpt(p.key,(p.labels[lang]||p.labels.fr));});data.custom.forEach(p=>addOpt(p.key,p.name+' (custom)'));if(data.active_key)select.value=data.active_key;}async function refreshStatus(){try{const status=await fetchJSON('/api/status');const dict=I18N[lang]||I18N.fr;updateBanner(status.summary,false);document.getElementById('cctDay').value=status.light.cct.day;document.getElementById('cctWarm').value=status.light.cct.warm;document.getElementById('uvaSet').value=status.light.uva.set;document.getElementById('uvaClamp').value=status.light.uva.clamp;document.getElementById('uvbSet').value=status.light.uvb.set;document.getElementById('uvbClamp').value=status.light.uvb.clamp;document.getElementById('uvbPeriod').value=status.light.uvb.period_s;document.getElementById('uvbDuty').value=status.light.uvb.duty_pm;document.getElementById('skyMode').value=status.light.sky;document.getElementById('alarmState').textContent=status.alarms.muted?'Muted':'Active';document.getElementById('calUviMax').value=status.calibration.uvi_max.toFixed(2);document.getElementById('calDuty').value=status.calibration.last_duty_pm.toFixed(0);document.getElementById('calMeasured').value=status.calibration.last_uvi.toFixed(2);const table=document.getElementById('telemetryTable');table.innerHTML='';const uviValid=status.climate&&status.climate.uvi_valid;const uviFault=status.dome&&status.dome.uvi_fault;let uviText='--';if(uviValid){uviText=status.climate.uvi_measured.toFixed(2)+' (Δ '+status.climate.uvi_error.toFixed(2)+', '+status.climate.irradiance_uW_cm2.toFixed(1)+' µW/cm²)';}else if(uviFault){const faultText=dict.uvi_fault||'sensor fault';uviText=faultText;}else{uviText=status.env.uvi!==undefined?status.env.uvi.toFixed(2):'--';}const irrText=status.env.irradiance_uW_cm2!==undefined?status.env.irradiance_uW_cm2.toFixed(1):'--';const rows=[['Temp °C',status.env.temperature.toFixed(1)],['Hum %',status.env.humidity.toFixed(1)],['Press hPa',status.env.pressure.toFixed(1)],['UVI',uviText],['Irr µW/cm²',irrText],['Fan %',status.light.fan_pwm.toFixed(0)],['Heatsink °C',status.dome.heatsink_c.toFixed(1)]];rows.forEach(([k,v])=>{const tr=document.createElement('tr');const td1=document.createElement('td');td1.textContent=k;const td2=document.createElement('td');td2.textContent=v;tr.appendChild(td1);tr.appendChild(td2);table.appendChild(tr);});        const ota=status.ota||{};
        const controllerStatus=document.getElementById('controllerStatusText');
        if(controllerStatus)controllerStatus.textContent=describeOta(ota.controller);
        const domeStatus=document.getElementById('domeStatusText');
        if(domeStatus)domeStatus.textContent=describeOta(ota.dome);
chartState.points.push({tempNorm:Math.min(1,Math.max(0,(status.env.temperature-10)/30)),humNorm:Math.min(1,Math.max(0,status.env.humidity/100))});if(chartState.points.length>120)chartState.points.shift();renderChart();}catch(e){updateBanner('Status error: '+e.message,true);}}
    "document.getElementById('languageSelect').addEventListener('change',e=>{setLang(e.target.value);refreshSpecies();});document.getElementById('applyLight').addEventListener('click',async()=>{const payload={cct:{day:+cctDay.value,warm:+cctWarm.value},uva:{set:+uvaSet.value,clamp:+uvaClamp.value},uvb:{set:+uvbSet.value,clamp:+uvbClamp.value,period_s:+uvbPeriod.value,duty_pm:+uvbDuty.value},sky:+skyMode.value};await fetchJSON('/api/light/dome0',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});refreshStatus();});document.getElementById('applySpecies').addEventListener('click',async()=>{const key=document.getElementById('speciesSelect').value;await fetchJSON('/api/species/apply',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({key})});refreshSpecies();});document.getElementById('saveCustom').addEventListener('click',async()=>{const name=document.getElementById('customName').value.trim();if(!name){alert('Name required');return;}try{const schedule=JSON.parse(document.getElementById('customSchedule').value);await fetchJSON('/api/species/custom',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,schedule})});refreshSpecies();}catch(err){alert('Invalid JSON: '+err.message);}});document.getElementById('toggleMute').addEventListener('click',async()=>{const r=await fetchJSON('/api/alarms/mute',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({toggle:true})});document.getElementById('alarmState').textContent=r.muted?'Muted':'Active';});document.getElementById('applyCalibration').addEventListener('click',async()=>{await fetchJSON('/api/calibrate/uvb',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({duty_pm:+calDuty.value,uvi:+calMeasured.value,uvi_max:+calUviMax.value})});refreshStatus();});async function uploadFirmware(manifestId,binId,url,progressId){const dict=I18N[lang]||I18N.fr;const manifest=document.getElementById(manifestId).files[0];if(!manifest){alert(dict.manifest_required||'Manifest required');return;}const file=document.getElementById(binId).files[0];if(!file){alert(dict.firmware_required||'Firmware required');return;}const manifestText=await manifest.text();const progress=document.getElementById(progressId);if(progress)progress.value=0;const res=await fetch(url,{method:'POST',headers:{'X-OTA-Manifest':encodeManifest(manifestText),'Content-Type':'application/octet-stream'},body:file});if(!res.ok){throw new Error(await res.text());}if(progress)progress.value=100;try{await res.json();}catch(e){}await refreshStatus();}document.getElementById('flashController').addEventListener('click',()=>uploadFirmware('controllerManifest','controllerBin','/api/ota/controller','controllerProgress').catch(e=>alert(e.message)));document.getElementById('flashDome').addEventListener('click',()=>uploadFirmware('domeManifest','domeBin','/api/ota/dome','domeProgress').catch(e=>alert(e.message)));setLang('fr');refreshSpecies();refreshStatus();setInterval(refreshStatus,5000);</script></body></html>";

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
    cJSON *cct = cJSON_GetObjectItem(root, "cct");
    cJSON *uva = cJSON_GetObjectItem(root, "uva");
    cJSON *uvb = cJSON_GetObjectItem(root, "uvb");
    cJSON *sky = cJSON_GetObjectItem(root, "sky");
    if (!cct || !uva || !uvb) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
    }

    uint8_t cct_buf[4];
    wr16_le(&cct_buf[0], (uint16_t)cJSON_GetObjectItem(cct, "day")->valueint);
    wr16_le(&cct_buf[2], (uint16_t)cJSON_GetObjectItem(cct, "warm")->valueint);
    ESP_ERROR_CHECK_WITHOUT_ABORT(dome_bus_write(DOME_REG_BLOCK_CCT, cct_buf, sizeof(cct_buf)));

    uint8_t uva_buf[4];
    wr16_le(&uva_buf[0], (uint16_t)cJSON_GetObjectItem(uva, "set")->valueint);
    wr16_le(&uva_buf[2], (uint16_t)cJSON_GetObjectItem(uva, "clamp")->valueint);
    ESP_ERROR_CHECK_WITHOUT_ABORT(dome_bus_write(DOME_REG_BLOCK_UVA, uva_buf, sizeof(uva_buf)));

    float uvb_set = cJSON_GetObjectItem(uvb, "set")->valuedouble;
    float uvb_clamp = cJSON_GetObjectItem(uvb, "clamp")->valuedouble;
    float period = cJSON_GetObjectItem(uvb, "period_s")->valuedouble;
    float duty_req = cJSON_GetObjectItem(uvb, "duty_pm")->valuedouble;
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

    if (sky) {
        uint8_t sky_val = (uint8_t)sky->valueint;
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

static esp_err_t api_species_get(httpd_req_t *req)
{
    REQUIRE_AUTH_OR_RETURN(req);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    char active[32] = {0};
    if (species_profiles_get_active_key(active, sizeof(active)) == ESP_OK) {
        cJSON_AddStringToObject(root, "active_key", active);
    }
    cJSON *builtin = cJSON_AddArrayToObject(root, "builtin");
    for (size_t i = 0; i < species_profiles_builtin_count(); ++i) {
        const species_profile_t *profile = species_profiles_builtin(i);
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "key", profile->key);
        cJSON *labels = cJSON_AddObjectToObject(entry, "labels");
        cJSON_AddStringToObject(labels, "fr", profile->label_fr);
        cJSON_AddStringToObject(labels, "en", profile->label_en);
        cJSON_AddStringToObject(labels, "es", profile->label_es);
        cJSON_AddStringToObject(entry, "habitat", profile->habitat);
        cJSON *sched = schedule_to_json(&profile->schedule);
        cJSON_AddItemToObject(entry, "schedule", sched);
        cJSON_AddItemToArray(builtin, entry);
    }
    cJSON *custom = cJSON_AddArrayToObject(root, "custom");
    for (size_t i = 0;; ++i) {
        species_custom_profile_t entry;
        if (species_profiles_custom_get(i, &entry) != ESP_OK) {
            break;
        }
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "key", entry.key);
        cJSON_AddStringToObject(obj, "name", entry.name);
        cJSON *sched = schedule_to_json(&entry.schedule);
        cJSON_AddItemToObject(obj, "schedule", sched);
        cJSON_AddItemToArray(custom, obj);
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
    esp_err_t err = species_profiles_save_custom(name, &schedule, key, sizeof(key));
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
