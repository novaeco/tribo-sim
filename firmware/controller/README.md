# Terrarium S3 — Controller (baseline v0.1)

- Framework: ESP-IDF via PlatformIO
- MCU: ESP32-S3-WROOM-2-N32R16V
- Licence: MIT — voir [`LICENSE`](../../LICENSE). Inclure la notice MIT et le disclaimer lors de toute redistribution du firmware (sources ou binaires OTA).
- Features in this baseline:
  - I2C master init, optional TCA9548A channel select
  - DS3231 time read (if connected)
  - SSR GPIO outputs + Fan PWM timers (stubs)
  - Wi‑Fi AP+STA + minimal HTTP server
  - Dome I2C register read (STATUS @ 0x00)

## Build
1. Install VSCode + PlatformIO.
2. Open this folder and select environment `s3-wroom2-idf`.
3. Build & Flash. Default USB-CDC console @ 115200.
