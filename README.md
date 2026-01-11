# ESP32-P4 7-inch Panel - Reptile Panel

A feature-rich smart terrarium controller built on ESP32-P4 with a 7-inch touchscreen display.

## Features

- **7-inch MIPI-DSI Display** (1024x600, JD9165BA controller)
- **GT911 Capacitive Touch** with multi-touch support
- **ESP32-C6 Co-processor** for WiFi & Bluetooth via ESP-Hosted
- **LVGL 9.4 UI Framework** with rich graphics
- **Climate Control System** for terrarium management:
  - Temperature monitoring (hot/cold zones)
  - Humidity control with misting schedules
  - UV lighting schedules (Ferguson zones)
  - Equipment control (heating, pumps, lights)
- **Reptile Inventory Management**:
  - Species tracking with CITES compliance
  - Health records and feeding logs
  - Breeding records
  - Legal document generation
- **SD Card Storage** (SDMMC 4-bit mode)
- **SNTP Time Synchronization**

## Hardware

| Component | Description |
|-----------|-------------|
| MCU | ESP32-P4 (Dual Core + LP Core, 400MHz) |
| Display | 7" 1024x600 MIPI-DSI (JD9165BA) |
| Touch | GT911 (I2C: GPIO7/GPIO8) |
| WiFi/BLE | ESP32-C6 co-processor (ESP-Hosted v2.8.5) |
| Storage | MicroSD (SDMMC Slot 0) |
| Audio | ES8311 codec (currently disabled) |

## Build Requirements

- **ESP-IDF 6.1** (esp-idf-6.1-dev)
- **Target**: esp32p4

## Quick Start

```bash
# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Build
idf.py set-target esp32p4
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSBx flash monitor
```

## Project Structure

```
esp32p4_7inch_panel/
├── main/
│   ├── main.c                  # Application entry point
│   ├── climate_manager.c/h     # Climate control logic
│   ├── climate_types.h         # Data structures
│   ├── climate_presets.c/h     # Terrarium presets
│   ├── ui_climate.c/h          # LVGL UI components
│   └── slave_fw/               # ESP32-C6 firmware
├── sdkconfig.defaults          # Default configuration
├── partitions.csv              # Partition table
├── SECURITY.md                 # Security documentation
└── CMakeLists.txt              # Project build file
```

## Configuration

Key settings in `sdkconfig.defaults`:

| Setting | Value | Description |
|---------|-------|-------------|
| `CONFIG_IDF_TARGET` | esp32p4 | Target chip |
| `CONFIG_APP_LCD_H_RES` | 1024 | Horizontal resolution |
| `CONFIG_APP_LCD_V_RES` | 600 | Vertical resolution |
| `CONFIG_SPIRAM` | y | Enable PSRAM |
| `CONFIG_LV_COLOR_DEPTH` | 16 | RGB565 color |

## Security

See [SECURITY.md](SECURITY.md) for production security guidelines:
- NVS encryption for WiFi credentials
- Task watchdog configuration
- Flash encryption options

## Known Issues

1. **Audio disabled**: ESP-IDF 6.1 linker bug with `esp_driver_i2s`
2. **LVGL Kconfig warnings**: Conflict between lvgl and lvgl_backup in IDF

## License

Copyright (c) 2026. All rights reserved.

## Version History

- **v1.0** (2026-01-08): Initial release with climate control and reptile management
