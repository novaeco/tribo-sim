idf_component_register(
    SRCS "storage.c"
    INCLUDE_DIRS "."
    # Use the built-in spiffs component rather than esp_spiffs.  The
    # esp_spiffs component was removed in IDF 6.x【978374526207654†L282-L289】.  FatFS and SDMMC are used for optional SD card
    # storage support.
    REQUIRES spiffs fatfs sdmmc
)