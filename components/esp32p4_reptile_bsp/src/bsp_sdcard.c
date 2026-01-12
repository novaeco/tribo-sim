/**
 * @file bsp_sdcard.c
 * @brief SD Card Driver (SDMMC 4-bit mode)
 */

#include "bsp_reptile.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "BSP_SDCARD";

esp_err_t bsp_sdcard_mount(void)
{
    ESP_LOGI(TAG, "Mounting SD card...");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 10,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4; // 4-bit mode
    slot_config.gpio_cd = GPIO_NUM_NC;
    slot_config.gpio_wp = GPIO_NUM_NC;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully");
        sdmmc_card_print_info(stdout, card);
    } else {
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
