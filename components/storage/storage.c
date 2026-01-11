// Implementation of persistent storage for the reptile simulation
//
// Provides functions to initialise file systems (SPIFFS or SD card),
// save the current game state and load previously saved states.  If
// USE_SD_CARD is enabled and mounting the SD card fails, the code
// falls back to SPIFFS.

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
// Note: esp_spiffs.h has been removed in newer ESP‑IDF versions.  The
// SPIFFS component is still available, but we no longer include
// esp_spiffs.h here to avoid pulling in an obsolete component.  The
// file system is mounted with esp_vfs_spiffs_register() below.
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "storage.h"

static const char *TAG = "STORAGE";
static bool s_storage_initialised = false;

// Helper to get the path to the save file depending on whether SD is used
static const char *get_save_path(void)
{
    return CONFIG_USE_SD_CARD ? "/sdcard/save.txt" : "/spiffs/save.txt";
}

void storage_init(void)
{
    if (s_storage_initialised) {
        return;
    }
    esp_err_t ret;
    if (CONFIG_USE_SD_CARD) {
        // Mount the SD card (1‑bit or 4‑bit SDMMC depending on board)
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5
        };
        sdmmc_card_t *card;
        ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Carte SD montée avec succès");
            s_storage_initialised = true;
            return;
        } else {
            ESP_LOGE(TAG, "Montage SD échoué (0x%x), fallback sur SPIFFS", ret);
        }
    }
    // Mount SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Montage SPIFFS échoué (0x%x)", ret);
    } else {
        // In ESP‑IDF v6.x and later, the esp_spiffs component has been removed, so
        // esp_spiffs_info() is no longer available.  We simply log a generic
        // message indicating SPIFFS was mounted.
        ESP_LOGI(TAG, "SPIFFS monté avec succès");
        s_storage_initialised = true;
    }
}

bool storage_save_state(const ReptileState *state)
{
    storage_init();
    const char *path = get_save_path();
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "Impossible d'ouvrir %s pour écriture", path);
        return false;
    }
    // Write values in a space separated line
    int res = fprintf(f, "%d %d %d %.2f %d\n",
                      state->health,
                      state->hunger,
                      state->growth,
                      state->temperature,
                      state->heater_on ? 1 : 0);
    fclose(f);
    return res > 0;
}

bool storage_load_state(ReptileState *state)
{
    storage_init();
    const char *path = get_save_path();
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGI(TAG, "Aucune sauvegarde trouvée à %s", path);
        return false;
    }
    int heater_flag = 0;
    int read = fscanf(f, "%d %d %d %f %d",
                      &state->health,
                      &state->hunger,
                      &state->growth,
                      &state->temperature,
                      &heater_flag);
    fclose(f);
    if (read == 5) {
        state->heater_on = (heater_flag != 0);
        return true;
    }
    ESP_LOGW(TAG, "Format de sauvegarde invalide (lignes lues: %d)", read);
    return false;
}