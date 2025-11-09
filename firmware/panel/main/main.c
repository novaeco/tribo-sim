#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "display_driver.h"
#include "network_manager.h"
#include "ui_main.h"

static const char *TAG = "main";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

void app_main(void)
{
    init_nvs();

    app_config_t config;
    esp_err_t cfg_err = app_config_load(&config);
    if (cfg_err != ESP_OK && cfg_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Chargement configuration échoué: %s", esp_err_to_name(cfg_err));
    }

    ESP_ERROR_CHECK(display_driver_init());
    ESP_ERROR_CHECK(ui_init(&config));

    esp_err_t err = network_manager_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network init failed: %s", esp_err_to_name(err));
        ui_show_error(err, "Init réseau");
    }
}
