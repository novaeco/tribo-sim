// Implementation of persistent storage for the reptile simulation
//
// Provides functions to initialise file systems (SPIFFS or SD card),
// save the current game state and load previously saved states.
// Updated to support new ReptileState fields.

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "storage.h"

static const char *TAG = "STORAGE";
static bool s_storage_initialised = false;

// Save file version for compatibility
#define SAVE_VERSION 2

// Helper to get the path to the save file depending on whether SD is used
static const char *get_save_path(void)
{
#ifdef CONFIG_USE_SD_CARD
    return "/sdcard/save.dat";
#else
    return "/spiffs/save.dat";
#endif
}

void storage_init(void)
{
    if (s_storage_initialised) {
        return;
    }
    esp_err_t ret;

#if CONFIG_USE_SD_CARD
    // Mount the SD card (1-bit or 4-bit SDMMC depending on board)
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5
    };
    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully");
        s_storage_initialised = true;
        return;
    } else {
        ESP_LOGE(TAG, "SD mount failed (0x%x), falling back to SPIFFS", ret);
    }
#endif

    // Mount SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed (0x%x)", ret);
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info("storage", &total, &used);
        ESP_LOGI(TAG, "SPIFFS mounted: %zu/%zu bytes used", used, total);
        s_storage_initialised = true;
    }
}

bool storage_save_state(const ReptileState *state)
{
    storage_init();
    const char *path = get_save_path();
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s for writing", path);
        return false;
    }

    // Write save file with version and all fields
    int res = fprintf(f, "%d\n"          // version
                         "%d %d %d %.2f %d\n"   // health hunger growth temp heater
                         "%d %d %d %u %d\n",   // cleanliness happiness mood age sleeping
                      SAVE_VERSION,
                      state->health,
                      state->hunger,
                      state->growth,
                      state->temperature,
                      state->heater_on ? 1 : 0,
                      state->cleanliness,
                      state->happiness,
                      (int)state->mood,
                      (unsigned)state->age_ticks,
                      state->is_sleeping ? 1 : 0);
    fclose(f);

    if (res > 0) {
        ESP_LOGD(TAG, "State saved successfully");
        return true;
    }
    return false;
}

bool storage_load_state(ReptileState *state)
{
    storage_init();
    const char *path = get_save_path();
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGI(TAG, "No save file found at %s", path);
        return false;
    }

    int version = 0;
    int read = fscanf(f, "%d", &version);

    if (read != 1) {
        // Try old format (version 1 without version header)
        rewind(f);
        int heater_flag = 0;
        read = fscanf(f, "%d %d %d %f %d",
                      &state->health,
                      &state->hunger,
                      &state->growth,
                      &state->temperature,
                      &heater_flag);
        fclose(f);
        if (read == 5) {
            state->heater_on = (heater_flag != 0);
            // Set defaults for new fields
            state->cleanliness = 100;
            state->happiness = 80;
            state->mood = MOOD_HAPPY;
            state->age_ticks = 0;
            state->is_sleeping = false;
            ESP_LOGI(TAG, "Loaded v1 save, migrating to v2");
            return true;
        }
        ESP_LOGW(TAG, "Invalid save format");
        return false;
    }

    if (version == 2) {
        int heater_flag = 0, mood_val = 0, sleeping_flag = 0;
        unsigned age = 0;
        read = fscanf(f, "%d %d %d %f %d %d %d %d %u %d",
                      &state->health,
                      &state->hunger,
                      &state->growth,
                      &state->temperature,
                      &heater_flag,
                      &state->cleanliness,
                      &state->happiness,
                      &mood_val,
                      &age,
                      &sleeping_flag);
        fclose(f);
        if (read == 10) {
            state->heater_on = (heater_flag != 0);
            state->mood = (ReptileMood)mood_val;
            state->age_ticks = age;
            state->is_sleeping = (sleeping_flag != 0);
            ESP_LOGI(TAG, "Loaded v2 save successfully");
            return true;
        }
    }

    fclose(f);
    ESP_LOGW(TAG, "Unknown save version %d or invalid format", version);
    return false;
}
