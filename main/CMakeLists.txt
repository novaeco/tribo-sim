idf_component_register(
    SRCS "main.c" "game_engine.c" "battle_system.c" "ui_game.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_lcd_st7701
        esp_lvgl_port
        lvgl
        esp_lcd_touch
        esp_lcd_touch_gt911
        esp_wifi_remote
        esp_h264
        esp_codec_dev
        button
        knob
        esp32-camera
        esp_io_expander_tca95xx_16bit
        image_player
        adc_mic
        esp_mmap_assets
        adc_battery_estimation
    PRIV_REQUIRES
        nvs_flash
        driver
        cjson
        fatfs
        sdmmc
        vfs
        esp_driver_sdmmc
        esp_wifi
        esp_netif
        esp_event
        esp_hosted
        bt
        esp_partition
        esp_app_format
)

# ESP32-C6 slave firmware for OTA updates
set(SLAVE_FW_BIN "${CMAKE_CURRENT_SOURCE_DIR}/slave_fw/network_adapter_esp32c6.bin")
if(EXISTS ${SLAVE_FW_BIN})
    message(STATUS "ESP32-C6 slave firmware found: ${SLAVE_FW_BIN}")
    # Embed the binary directly into the application
    target_add_binary_data(${COMPONENT_LIB} "${SLAVE_FW_BIN}" BINARY)
else()
    message(WARNING "ESP32-C6 slave firmware NOT found at: ${SLAVE_FW_BIN}")
endif()
