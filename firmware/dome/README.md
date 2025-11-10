# Terrarium S3 — Dome (baseline v0.1)

- Framework: ESP-IDF via PlatformIO
- MCU: ESP32-S3-WROOM-2-N32R16V
- Licence: MIT — voir [`LICENSE`](../../LICENSE). Toute redistribution (code, binaires, documentation associée) doit conserver la notice MIT.
- Features in this baseline:
  - I2C slave @ 0x3A (very simple register protocol for demo)
  - LEDC PWM (CH1/CH2 whites, CH3 UVA, CH4 UVB)
  - WS2812 RMT stub
  - Fan PWM init
  - NTC ADC telemetry with Beta-model conversion and oversampling

## Build
1. Install VSCode + PlatformIO.
2. Open this folder and select environment `s3-wroom2-idf`.
3. Build & Flash.

## Thermal sensing constants & accuracy

- Thermistor: 10 kΩ @ 25 °C, Beta = 3950 K (Beta equation).
- Bias network: 10 kΩ ±1 % pull-up to the regulated 3.30 V rail (measured).
- ADC: ADC1/CH2, 11 dB attenuation, oversampling on 8 calibrated conversions (falling back to raw codes if calibration unsupported).
- Numerical model: Beta equation evaluated after resistance reconstruction, returning `NAN` and logging errors when measurements fall outside the divider range.
- Expected accuracy: ±0.7 °C (Beta fit, resistor tolerance, ADC INL) within −10…110 °C assuming supply within ±1 % of 3.30 V.
