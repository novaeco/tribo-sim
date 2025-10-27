#include "httpd.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "drivers/dome_i2c.h"
#include "drivers/dome_bus.h"
#include "drivers/sensors.h"
#include "drivers/calib.h"
#include "include/config.h"

static esp_err_t dome_bus_read(uint8_t reg, uint8_t* b, size_t n){
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_DOME0);
#endif
    esp_err_t r = dome_bus_read( reg, b, n);
    if (r==ESP_OK){ dome_i2c_errs = 0; }
    else { if (++dome_i2c_errs > 5) dome_bus_is_degraded() = true; }
    return r;
}
static esp_err_t dome_bus_write(uint8_t reg, const uint8_t* b, size_t n){
    if (dome_bus_is_degraded()) return ESP_FAIL; // do not push writes when degraded
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_DOME0);
#endif
    esp_err_t r = dome_bus_write( reg, b, n);
    if (r==ESP_OK){ dome_i2c_errs = 0; }
    else { if (++dome_i2c_errs > 5) dome_bus_is_degraded() = true; }
    return r;
}
static const char* TAG="HTTPD";


static esp_err_t root_get_handler(httpd_req_t *req){
    const char* html =
    "<!doctype html><html><head><meta charset='utf-8'><title>Terrarium S3</title>"
    "<style>body{font-family:system-ui;margin:24px;max-width:900px}"
    "label{display:block;margin-top:10px}input[type=range]{width:100%}"
    ".row{display:grid;grid-template-columns:220px 1fr;gap:12px;align-items:center}"
    "button{padding:8px 12px;margin-top:12px}</style></head><body>"
    "<h1>Terrarium S3 — Dôme</h1>"
    "<div id='status'></div>"
    "<div class='row'><label>Day (‰)</label><input id='day' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>Warm (‰)</label><input id='warm' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>UVA (‰)</label><input id='uva' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>UVB (‰) — UVI limited</label><input id='uvb' type='range' min='0' max='10000'></div>"
    "<div class='row'><label>UVB period (s)</label><input id='uvb_per' type='number' min='5' max='600' value='60'></div>"
    "<div class='row'><label>UVB duty (‰)</label><input id='uvb_duty' type='number' min='0' max='10000' value='1000'></div>"
    "<div class='row'><label>Sky</label><select id='sky'><option value='0'>Off</option><option value='1'>Blue</option><option value='2'>Twinkle</option></select></div>"
    "<button onclick='save()'>Apply</button>"
    "<h2 style=margin-top:32px>Capteurs</h2><pre id='capteurs'></pre>"
    "<h2>Calibration UVB</h2>"
    "<div class='row'><label>UVI max (target)</label><input id='uvi_max' type='number' step='0.1' value='1.0'></div>"
    "<div class='row'><label>Dernier k</label><span id='kval'></span></div>"
    "<div class='row'><label>Mesure UVI @ duty (‰)</label><input id='cal_duty' type='number' min='0' max='10000' value='1000'></div>"
    "<div class='row'><label>UVI mesuré</label><input id='cal_uvi' type='number' step='0.01' value='0.1'></div>"
    "<button onclick='calib()'>Enregistrer calibration</button>"
    "<script>"
    "async function refresh(){"
    "  let r=await fetch('/api/light/dome0'); let j=await r.json();"
    "  day.value=j.cct.day; warm.value=j.cct.warm; uva.value=j.uva.set; uvb.value=j.uvb.set;"
    "  uvb_per.value=j.uvb.period_s; uvb_duty.value=j.uvb.duty_pm; sky.value=j.sky;"
    "  status.innerText='STATUS 0x'+j.status.toString(16);"
    "  let s=await (await fetch('/api/status')).json();"
    "  capteurs.textContent=JSON.stringify(s,null,2);"
    "  let kinfo=await (await fetch('/api/calibrate/uvb')).json();"
    "  uvi_max.value=kinfo.uvi_max; kval.textContent=kinfo.k.toFixed(6)+' UVI/‰';"
    "}"
    "async function save(){"
    " const p={cct:{day:+day.value,warm:+warm.value}, uva:{set:+uva.value},"
    " uvb:{set:+uvb.value,period_s:+uvb_per.value,duty_pm:+uvb_duty.value}, sky:+sky.value};"
    " await fetch('/api/light/dome0',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});"
    " refresh(); }"
    "async function calib(){"
    " const body={duty_pm:+cal_duty.value, uvi:+cal_uvi.value, uvi_max:+uvi_max.value};"
    " await fetch('/api/calibrate/uvb',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    " refresh(); }"
    "refresh();"
    "</script></body></html>";
    httpd_resp_send(req, html, -1);
    return ESP_OK;
}

static esp_err_t api_get_handler(httpd_req_t *req){
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_DOME0);
#endif
    uint8_t cct1_l=0,cct1_h=0,cct2_l=0,cct2_h=0, uva=0, uvb=0, sky=0, per=0, duty=0, status=0;
    dome_bus_read( 0x00, &status, 1);
    dome_bus_read( 0x02, &cct1_l, 1);
    dome_bus_read( 0x03, &cct1_h, 1);
    dome_bus_read( 0x04, &cct2_l, 1);
    dome_bus_read( 0x05, &cct2_h, 1);
    dome_bus_read( 0x06, &uva, 1);
    dome_bus_read( 0x07, &uvb, 1);
    dome_bus_read( 0x08, &sky, 1);
    dome_bus_read( 0x0B, &per, 1);
    dome_bus_read( 0x0C, &duty, 1);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j,"status",status);
    cJSON *cct = cJSON_AddObjectToObject(j,"cct");
    cJSON_AddNumberToObject(cct,"day", (int)(cct1_l | (cct1_h<<8)));
    cJSON_AddNumberToObject(cct,"warm",(int)(cct2_l | (cct2_h<<8)));
    cJSON *juva = cJSON_AddObjectToObject(j,"uva");
    cJSON_AddNumberToObject(juva,"set",uva*100);
    cJSON *juvb = cJSON_AddObjectToObject(j,"uvb");
    cJSON_AddNumberToObject(juvb,"set",uvb*100);
    cJSON_AddNumberToObject(juvb,"period_s", per);
    cJSON_AddNumberToObject(juvb,"duty_pm", duty);
    cJSON_AddNumberToObject(j,"sky", sky);
    httpd_resp_set_type(req, "application/json");
    char* out = cJSON_PrintUnformatted(j);
    httpd_resp_sendstr(req, out);
    free(out); cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t api_post_handler(httpd_req_t *req){
    char buf[512]; int rlen = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (rlen <= 0) return ESP_FAIL; buf[rlen]=0;
    cJSON *j = cJSON_Parse(buf); if(!j) return ESP_FAIL;

    cJSON *cct = cJSON_GetObjectItem(j,"cct");
    cJSON *uva = cJSON_GetObjectItem(j,"uva");
    cJSON *uvb = cJSON_GetObjectItem(j,"uvb");
    cJSON *sky = cJSON_GetObjectItem(j,"sky");

#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_DOME0);
#endif
    // Write CCT1/2
    int day = cJSON_GetObjectItem(cct,"day")->valueint;
    int warm= cJSON_GetObjectItem(cct,"warm")->valueint;
    uint8_t p[4] = { day & 0xFF, (day>>8)&0xFF, warm & 0xFF, (warm>>8)&0xFF };
    dome_bus_write( 0x02, p, 4);

    // UVA set (permille)
    int uva_set = cJSON_GetObjectItem(uva,"set")->valueint;
    uint8_t uva_b = (uint8_t)(uva_set/100);
    dome_bus_write( 0x06, &uva_b, 1);

    // UVB with UVI clamp via calibration k
    float k=0, uvi_max=0; calib_get_uvb(&k,&uvi_max);
    int uvb_set = cJSON_GetObjectItem(uvb,"set")->valueint; // permille
    float allowed_duty_pm = (uvi_max>0 && k>0) ? (uvi_max / k) : uvb_set;
    if (uvb_set > (int)allowed_duty_pm) uvb_set = (int)allowed_duty_pm;
    uint8_t uvb_b = (uint8_t)(uvb_set/100);
    dome_bus_write( 0x07, &uvb_b, 1);
    // period + duty
    int uvb_per = cJSON_GetObjectItem(uvb,"period_s")->valueint;
    int uvb_duty= cJSON_GetObjectItem(uvb,"duty_pm")->valueint;
    uint8_t per_b = (uint8_t)uvb_per, duty_b = (uint8_t)uvb_duty;
    dome_bus_write( 0x0B, &per_b, 1);
    dome_bus_write( 0x0C, &duty_b, 1);

    // Sky
    uint8_t sky_b = (uint8_t)sky->valueint;
    dome_bus_write( 0x08, &sky_b, 1);

    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{"ok":true}");
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req){
    terra_sensors_t s = {0};
    sensors_read(&s);
#if TCA_PRESENT
    tca9548a_select(I2C_NUM_0, TCA_ADDR, TCA_CH_DOME0);
#endif
    uint8_t status=0, theat=0;
    dome_bus_read( 0x00, &status, 1);
    dome_bus_read( 0x20, &theat, 1);

    cJSON *j = cJSON_CreateObject();
    cJSON *sens = cJSON_AddObjectToObject(j,"sensors");
    cJSON_AddNumberToObject(sens,"ds18b20_bus1_c", s.t1_c);
    cJSON_AddBoolToObject(sens,"ds18b20_bus1_present", s.t1_present);
    cJSON_AddNumberToObject(sens,"ds18b20_bus2_c", s.t2_c);
    cJSON_AddBoolToObject(sens,"ds18b20_bus2_present", s.t2_present);
    cJSON_AddNumberToObject(sens,"sht31_t_c", s.sht31_t_c);
    cJSON_AddBoolToObject(sens,"sht31_present", s.sht31_present);
    cJSON_AddNumberToObject(sens,"sht31_rh", s.sht31_rh);
    cJSON_AddNumberToObject(sens,"sht21_t_c", s.sht21_t_c);
    cJSON_AddNumberToObject(sens,"sht21_rh", s.sht21_rh);
    cJSON_AddBoolToObject(sens,"sht21_present", s.sht21_present);
    cJSON_AddNumberToObject(sens,"bme280_t_c", s.bme_t_c);
    cJSON_AddNumberToObject(sens,"bme280_rh", s.bme_rh);
    cJSON_AddNumberToObject(sens,"bme280_p_hpa", s.bme_p_hpa);
    cJSON_AddBoolToObject(sens,"bme280_present", s.bme_present);
    cJSON *d = cJSON_AddObjectToObject(j,"dome");
    cJSON_AddNumberToObject(d,"status", status);
    cJSON_AddBoolToObject(d,"interlock", (status & (1<<5))!=0);
    cJSON_AddBoolToObject(d,"therm_hard", (status & (1<<6))!=0);
    cJSON_AddBoolToObject(d,"ot_soft", (status & 1)!=0);
    cJSON_AddBoolToObject(d,"bus_loss_degraded", dome_bus_is_degraded());
    cJSON_AddNumberToObject(d,"t_heatsink_c", (int)theat);
    char* out = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out); cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t calib_get_handler(httpd_req_t *req){
    float k=0, uvi_max=0; calib_get_uvb(&k,&uvi_max);
    cJSON *j=cJSON_CreateObject();
    cJSON_AddNumberToObject(j,"k",k);
    cJSON_AddNumberToObject(j,"uvi_max",uvi_max);
    char* out=cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_sendstr(req,out);
    free(out); cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t calib_post_handler(httpd_req_t *req){
    char buf[256]; int r=httpd_req_recv(req, buf, sizeof(buf)-1); if(r<=0) return ESP_FAIL; buf[r]=0;
    cJSON* j=cJSON_Parse(buf); if(!j) return ESP_FAIL;
    float duty_pm = cJSON_GetObjectItem(j,"duty_pm")->valuedouble;
    float uvi     = cJSON_GetObjectItem(j,"uvi")->valuedouble;
    float uvi_max = cJSON_GetObjectItem(j,"uvi_max")->valuedouble;
    calib_init();
    if (uvi_max>0) calib_set_uvb_uvi_max(uvi_max);
    if (duty_pm>0 && uvi>0) calib_set_uvb(duty_pm, uvi);
    cJSON_Delete(j);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_sendstr(req,"{"ok":true}");
    return ESP_OK;
}

void httpd_start_basic(void){
    calib_init();
    sensors_init();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=root_get_handler, .user_ctx=NULL};
        httpd_uri_t apig = {.uri="/api/light/dome0", .method=HTTP_GET, .handler=api_get_handler, .user_ctx=NULL};
        httpd_uri_t apip = {.uri="/api/light/dome0", .method=HTTP_POST, .handler=api_post_handler, .user_ctx=NULL};
        httpd_uri_t st   = {.uri="/api/status", .method=HTTP_GET, .handler=status_handler, .user_ctx=NULL};
        httpd_uri_t cg   = {.uri="/api/calibrate/uvb", .method=HTTP_GET, .handler=calib_get_handler, .user_ctx=NULL};
        httpd_uri_t cp   = {.uri="/api/calibrate/uvb", .method=HTTP_POST, .handler=calib_post_handler, .user_ctx=NULL};
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &apig);
        httpd_register_uri_handler(server, &apip);
        httpd_register_uri_handler(server, &st);
        httpd_register_uri_handler(server, &cg);
        httpd_register_uri_handler(server, &cp);
        ESP_LOGI(TAG, "HTTP server started");
    }
}
