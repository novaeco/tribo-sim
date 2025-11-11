#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI";
static const char *WIFI_NVS_NAMESPACE = "wifi";
static const char *WIFI_NVS_KEY_STA = "sta_cfg";

static bool s_netif_initialized = false;
static bool s_event_loop_initialized = false;
static bool s_wifi_driver_initialized = false;
static bool s_nvs_initialized = false;
static bool s_event_handlers_registered = false;
static bool s_sta_config_valid = false;

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_event_handler_instance_t s_wifi_event_instance = NULL;
static esp_event_handler_instance_t s_ip_event_instance = NULL;
static wifi_sta_config_t s_sta_config = {0};

static esp_err_t wifi_init_nvs(void);
static esp_err_t wifi_load_sta_config_from_nvs(void);
static esp_err_t wifi_save_sta_config_to_nvs(const wifi_sta_config_t *cfg);
static esp_err_t wifi_prepare_network_interfaces(void);
static esp_err_t wifi_register_event_handlers(void);
static void wifi_default_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static esp_err_t wifi_init_nvs(void) {
    if (s_nvs_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased (%s)", esp_err_to_name(err));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS partition: %s", esp_err_to_name(erase_err));
            return erase_err;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }

    s_nvs_initialized = true;
    return ESP_OK;
}

static esp_err_t wifi_load_sta_config_from_nvs(void) {
    esp_err_t err = wifi_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No STA configuration found in NVS");
        return err;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", WIFI_NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    wifi_sta_config_t cfg = {0};
    size_t required_size = sizeof(cfg);
    err = nvs_get_blob(handle, WIFI_NVS_KEY_STA, &cfg, &required_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "STA configuration blob not present in NVS");
        return err;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read STA configuration from NVS: %s", esp_err_to_name(err));
        return err;
    }

    if (required_size != sizeof(cfg)) {
        ESP_LOGW(TAG, "Unexpected STA configuration blob size (%zu)", required_size);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(&s_sta_config, &cfg, sizeof(cfg));
    s_sta_config_valid = true;
    ESP_LOGI(TAG, "Loaded STA configuration for SSID '%s'", (const char *)s_sta_config.ssid);
    return ESP_OK;
}

static esp_err_t wifi_save_sta_config_to_nvs(const wifi_sta_config_t *cfg) {
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s' for writing: %s", WIFI_NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, WIFI_NVS_KEY_STA, cfg, sizeof(*cfg));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist STA configuration: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Persisted STA configuration for SSID '%s'", (const char *)cfg->ssid);
    }

    return err;
}

static esp_err_t wifi_prepare_network_interfaces(void) {
    if (!s_netif_initialized) {
        esp_err_t err = esp_netif_init();
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_netif_init already called");
            s_netif_initialized = true;
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize esp_netif: %s", esp_err_to_name(err));
            return err;
        } else {
            s_netif_initialized = true;
        }
    }

    if (!s_event_loop_initialized) {
        esp_err_t err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Event loop already created");
            s_event_loop_initialized = true;
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
            return err;
        } else {
            s_event_loop_initialized = true;
        }
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create default STA netif");
            return ESP_FAIL;
        }
    }

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create default AP netif");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static void wifi_default_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                ESP_LOGI(TAG, "STA interface started");
                if (s_sta_config_valid && s_sta_config.ssid[0] != '\0') {
                    esp_err_t err = esp_wifi_connect();
                    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                        ESP_LOGW(TAG, "Failed to initiate STA connection: %s", esp_err_to_name(err));
                    }
                }
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                const wifi_event_sta_disconnected_t *disconn = (const wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "STA disconnected, reason=%d", disconn ? disconn->reason : -1);
                if (s_sta_config_valid && s_sta_config.ssid[0] != '\0') {
                    esp_err_t err = esp_wifi_connect();
                    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                        ESP_LOGW(TAG, "Failed to reconnect STA: %s", esp_err_to_name(err));
                    }
                }
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED: {
                const wifi_event_ap_staconnected_t *conn = (const wifi_event_ap_staconnected_t *)event_data;
                if (conn != NULL) {
                    ESP_LOGI(TAG, "Station " MACSTR " connected to AP", MAC2STR(conn->mac));
                }
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                const wifi_event_ap_stadisconnected_t *disc = (const wifi_event_ap_stadisconnected_t *)event_data;
                if (disc != NULL) {
                    ESP_LOGI(TAG, "Station " MACSTR " disconnected from AP", MAC2STR(disc->mac));
                }
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
            if (event != NULL) {
                ESP_LOGI(TAG, "STA obtained IP: " IPSTR, IP2STR(&event->ip_info.ip));
            }
        }
    }
}

static esp_err_t wifi_register_event_handlers(void) {
    if (s_event_handlers_registered) {
        return ESP_OK;
    }

    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_default_event_handler, NULL, &s_wifi_event_instance);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "WIFI_EVENT handler already registered");
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_default_event_handler, NULL, &s_ip_event_instance);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "IP_EVENT handler already registered");
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
        if (s_wifi_event_instance != NULL) {
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
            s_wifi_event_instance = NULL;
        }
        return err;
    }

    s_event_handlers_registered = true;
    return ESP_OK;
}

esp_err_t wifi_set_sta_config(const wifi_sta_config_t *cfg, bool persist) {
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_sta_config, cfg, sizeof(*cfg));
    s_sta_config_valid = true;

    if (persist) {
        esp_err_t err = wifi_save_sta_config_to_nvs(cfg);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t wifi_load_sta_config(void) {
    esp_err_t err = wifi_load_sta_config_from_nvs();
    if (err == ESP_OK) {
        return ESP_OK;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }

    return err;
}

bool wifi_has_sta_config(void) {
    return s_sta_config_valid && s_sta_config.ssid[0] != '\0';
}

static void wifi_prepare_ap_config(wifi_config_t *ap_config, const char *ssid, const char *pass) {
    memset(ap_config, 0, sizeof(*ap_config));
    strlcpy((char *)ap_config->ap.ssid, ssid, sizeof(ap_config->ap.ssid));
    size_t pass_len = 0;
    if (pass != NULL) {
        strlcpy((char *)ap_config->ap.password, pass, sizeof(ap_config->ap.password));
        pass_len = strnlen(pass, sizeof(ap_config->ap.password));
    }
    ap_config->ap.max_connection = 4;
    if (pass_len == 0) {
        ap_config->ap.authmode = WIFI_AUTH_OPEN;
    } else if (pass_len < 8) {
        ESP_LOGW(TAG, "AP password too short (%u), falling back to open auth", (unsigned)pass_len);
        memset(ap_config->ap.password, 0, sizeof(ap_config->ap.password));
        ap_config->ap.authmode = WIFI_AUTH_OPEN;
    } else {
        ap_config->ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        ap_config->ap.pmf_cfg.required = false;
    }
}

static esp_err_t wifi_apply_sta_config(void) {
    if (!s_sta_config_valid || s_sta_config.ssid[0] == '\0') {
        ESP_LOGW(TAG, "STA configuration missing, skipping STA setup");
        return ESP_OK;
    }

    wifi_config_t cfg = {0};
    cfg.sta = s_sta_config;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA configuration: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_start_apsta(const char *ssid, const char *pass) {
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "AP SSID must be provided");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_prepare_network_interfaces();
    if (err != ESP_OK) {
        return err;
    }

    err = wifi_register_event_handlers();
    if (err != ESP_OK) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (!s_wifi_driver_initialized) {
        err = esp_wifi_init(&cfg);
        if (err == ESP_ERR_WIFI_INIT_STATE) {
            ESP_LOGW(TAG, "Wi-Fi driver already initialized");
            s_wifi_driver_initialized = true;
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize Wi-Fi driver: %s", esp_err_to_name(err));
            return err;
        } else {
            s_wifi_driver_initialized = true;
        }
    }

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t ap_config;
    wifi_prepare_ap_config(&ap_config, ssid, pass ? pass : "");
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP configuration: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_sta_config_valid) {
        esp_err_t load_err = wifi_load_sta_config_from_nvs();
        if (load_err == ESP_OK) {
            ESP_LOGI(TAG, "STA configuration loaded from NVS");
        } else if (load_err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "No persisted STA configuration found");
        } else {
            ESP_LOGE(TAG, "Failed to load STA configuration from NVS: %s", esp_err_to_name(load_err));
        }
    }

    err = wifi_apply_sta_config();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_CONN || err == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Wi-Fi already started");
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    if (wifi_has_sta_config()) {
        esp_err_t connect_err = esp_wifi_connect();
        if (connect_err != ESP_OK && connect_err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, "Failed to connect STA interface: %s", esp_err_to_name(connect_err));
        }
    }

    ESP_LOGI(TAG, "AP+STA started (AP SSID: %s)", ssid);
    return ESP_OK;
}
