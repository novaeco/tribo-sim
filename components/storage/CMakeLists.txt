idf_component_register(
    SRCS "storage.c"
    INCLUDE_DIRS "."
    # Dependencies:
    # - types: For ReptileState type
    # - spiffs: SPIFFS filesystem
    # - fatfs: FAT filesystem for SD card
    # - sdmmc: SD/MMC driver for SD card
    REQUIRES types spiffs fatfs sdmmc
)
