# Terrarium Panel Firmware

Firmware ESP-IDF pour le panneau Waveshare ESP32-S3 Touch LCD 7B utilisé comme interface locale du contrôleur de terrarium. Le projet intègre LVGL v9, l'afficheur RGB 1024×600 et le contrôleur tactile GT911.

## Fonctionnalités

- Connexion Wi-Fi station configurable (SSID/mot de passe) avec stockage dans NVS.
- Client REST communiquant avec le contrôleur via `/api/status`, `/api/light/dome0`, `/api/calibrate/uvb` et `/api/alarms/mute`.
- Interface LVGL avec page de contrôle (sliders CCT/UVA/UVB, sélection du mode sky, mute des alarmes) et page de calibration UVB.
- Double buffer LVGL en PSRAM, rafraîchissement 10 ms, support tactile GT911.
- Gestion réseau et rafraîchissement d'écran répartis en tâches FreeRTOS.
- Configuration OTA dual-bank et console USB-CDC activées via `sdkconfig.defaults`.

## Structure du projet

```
firmware/panel/
├── CMakeLists.txt
├── idf_component.yml
├── main/
├── components/
│   ├── app_config/
│   ├── drivers/
│   ├── network/
│   └── ui/
├── partitions.csv
└── sdkconfig.defaults
```

## Compilation et flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Assurez-vous d'utiliser une toolchain ESP-IDF v5.1+ avec le support LVGL 9 depuis [component registry](https://components.espressif.com/components/lvgl/lvgl).

## Configuration

Les paramètres réseau sont stockés dans NVS (`app_config`). Ils peuvent être édités via l'onglet « Calibration UVI » de l'interface LVGL puis sauvegardés. Un redémarrage est requis pour appliquer une nouvelle configuration Wi-Fi.

Les valeurs par défaut (modifiable dans `app_config.h`) :

- SSID : `terrarium-s3`
- Mot de passe : `terrarium123`
- Hôte contrôleur : `192.168.4.1`
- Port : `80`

