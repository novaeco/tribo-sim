#include "storage.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "storage";

#ifndef CONFIG_NVS_ENCRYPTION_PARTITION
#define CONFIG_NVS_ENCRYPTION_PARTITION "nvs_keys"
#endif

esp_err_t storage_secure_erase(void)
{
    esp_err_t err = nvs_flash_deinit();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) {
        return err;
    }
    return nvs_flash_erase();
}

esp_err_t storage_secure_deinit(void)
{
    esp_err_t err = nvs_flash_deinit();
    if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
        return ESP_OK;
    }
    return err;
}

esp_err_t storage_secure_init(void)
{
    esp_err_t err;
#ifdef CONFIG_NVS_ENCRYPTION
    nvs_sec_cfg_t cfg = {0};
    err = nvs_flash_read_security_cfg(CONFIG_NVS_ENCRYPTION_PARTITION, &cfg);
    if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
        ESP_LOGI(TAG, "Generating NVS encryption keys");
        ESP_RETURN_ON_ERROR(nvs_flash_generate_keys(CONFIG_NVS_ENCRYPTION_PARTITION), TAG, "nvs_flash_generate_keys failed");
        err = nvs_flash_read_security_cfg(CONFIG_NVS_ENCRYPTION_PARTITION, &cfg);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_read_security_cfg failed");
    err = nvs_flash_secure_init(&cfg);
#else
    err = nvs_flash_init();
#endif
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition requires erase (%s)", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(storage_secure_erase(), TAG, "nvs erase failed");
#ifdef CONFIG_NVS_ENCRYPTION
        err = nvs_flash_secure_init(&cfg);
#else
        err = nvs_flash_init();
#endif
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");
    ESP_LOGI(TAG, "NVS ready (%sencrypted)",
#ifdef CONFIG_NVS_ENCRYPTION
             ""
#else
             "not "
#endif
    );
    return ESP_OK;
}
