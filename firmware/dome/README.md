# Terrarium S3 — Dome (baseline v0.1)

- Framework: ESP-IDF via PlatformIO
- MCU: ESP32-S3-WROOM-2-N32R16V
- Features in this baseline:
  - I2C slave @ 0x3A (very simple register protocol for demo)
  - LEDC PWM (CH1/CH2 whites, CH3 UVA, CH4 UVB)
  - WS2812 RMT stub
  - Fan PWM init
  - NTC ADC rough read -> telemetry placeholder

## Build
1. Install VSCode + PlatformIO.
2. Open this folder and select environment `s3-wroom2-idf`.
3. Build & Flash.
