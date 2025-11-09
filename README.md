
# Terrarium S3 — Contrôleur + Dôme + Panel (v0.4.0)

**MCU (x3)** : ESP32‑S3‑WROOM‑2‑**N32R16V** (OPI Flash 32 MB + OPI PSRAM 16 MB)
**Frameworks** : ESP‑IDF (PlatformIO pour contrôleur/dôme, ESP‑IDF pur pour panel) — USB‑CDC, Dual‑OTA, Wi‑Fi AP/STA, HTTP API, I²C maître/esclave, LVGL v9
**Scope** : Contrôleur terrarium (climat, schedules, API) + Dôme lumineux 24 V (CH1/CH2 CCT, CH3 **UVA**, CH4 **UVB**, **WS2812 Sky/Star**), sécurité photobiologique, interlocks, calibration UVI, **panel tactile 7” LVGL** (contrôle local + calibration).

> ⚠️ **Sécurité UV** : L’UVB est **biologiquement actif**. Respecter EN/IEC 62471, distances/temps d’exposition, capotage, **interlock capot**, et **thermostat hard 85–90 °C** en série LED. Ne jamais regarder les LED UV à l’œil nu.

---

## TL;DR (Quickstart)

1. **Contrôleur** : ouvrir `firmware/controller` dans VSCode + PlatformIO → build + flash (`board = esp32-s3-devkitc-1`).
2. **Dôme** : ouvrir `firmware/dome` → build + flash (même board).
3. **Panel tactile** : `cd firmware/panel` → `idf.py set-target esp32s3 && idf.py build flash monitor` (Waveshare ESP32-S3 Touch LCD 7B).
4. Le contrôleur démarre en **AP+STA** (`SSID: terrarium-s3`, `PASS: terrarium123`) + serveur **HTTP**. Le panel peut se connecter en STA (NVS) ou via l’AP contrôleur.
5. Interface web/panel → sliders CCT/UVA/UVB, UVB pulsé, **capteurs**, **mute alarmes**, **calibration UVI**, **régulation climatique**.
6. **Câblage** : I²C maître (SDA=8, SCL=9), dôme esclave @ **0x3A**, **INT** OD (GPIO1), **interlock capot** (GPIO17 dôme, actif bas).
7. **Interlock** : ouvrir le capot coupe les **UV** < 100 ms (soft) + thermostat **hard** (85–90 °C) **en série** CH1–CH4.

---

## Arborescence

```
/firmware
  /controller
    platformio.ini
    sdkconfig.defaults
    partitions.csv
    /main
      app_main.c
      include/config.h
      drivers/ (i2c_bus.c, tca9548a.c, pcf8574.c, ds3231.c,
                ssr.c, fans.c, dome_i2c.c, dome_bus.c,
                onewire.c, sht31.c, sht21.c, bme280.c,
                sensors.c, calib.c, alarms.c, climate.c)
      net/ (wifi.c, httpd.c)
  /dome
    platformio.ini
    sdkconfig.defaults
    partitions.csv
    /main
      app_main.c
      include/{config.h, regs.h}
      drivers/ (i2c_slave_if.c, ledc_cc.c, ws2812_rmt.c,
                fan_pwm.c, ntc_adc.c)
  /panel
    CMakeLists.txt
    sdkconfig.defaults
    partitions.csv
    idf_component.yml (LVGL v9)
    /components (drivers/, network/, ui/, app_config/)
    /main (app_main.c, wifi_task.c, ui_task.c)
/docs
  climate_control.md
  validation_plan.md
```

---

## Documentation

- `docs/climate_control.md` : machine d’états, profils jour/nuit, hystérésis et tâches FreeRTOS pour la régulation climatique.
- `docs/validation_plan.md` : protocoles de tests sécurité UV, capteurs, endurance, régulation climatique.
- `firmware/panel/README.md` : instructions spécifiques au panel LVGL (Waveshare ESP32-S3 Touch LCD 7B).

---

## Matériel (résumé)

### Contrôleur (TBTS)
- **Alim** : USB‑C 5 V + bornier 5–24 V ; buck 5→3,3 V ≥ 3 A, TVS/ESD.  
- **I²C maître** : SDA=GPIO8, SCL=GPIO9 ; **TCA9548A** (0x70) en option (routing capteurs/dômes).  
- **1‑Wire** : BUS1=GPIO14, BUS2=GPIO21 (DS18B20).  
- **Sorties** : 4× **SSR AC** (GPIO 10/11/12/13).  
- **Ventilos** : PWM (GPIO4/GPIO5), tachy (GPIO16/GPIO15).  
- **HMI** : LED statut (GPIO7), buzzer (GPIO6, LEDC), bouton (GPIO1).

### Dôme (24 V)
- **Entrée** 24 V → buck 5 V MCU ; TVS/ESD 24 V.  
- **Drivers CC** : CH1/CH2 (blancs), **CH3 UVA**, **CH4 UVB** (LED 3535/6868).  
- **WS2812** (GPIO18).  
- **Ventilateur** 40 mm PWM (GPIO4), tachy (GPIO5).  
- **Capteurs** : NTC dissipateur (ADC1 CH2), éventuellement SHT interne.  
- **Sécurité hard** : **thermostat 85–90 °C en série** CH1–CH4 (coupure physique).  
- **Interlock capot** : `DOME_INTERLOCK_GPIO=17`, **actif bas** (UV off < 100 ms).  
- **INT** OD vers contrôleur (GPIO1).  
- **Fenêtres UV** : **quartz** pour UVB ; verre trempé dépoli pour voie visible.

### Panel tactile (ESP32-S3 Touch LCD 7B)
- **Écran** : 7” 1024×600 RGB, double buffer LVGL (PSRAM) + VSYNC 10 ms.
- **Tactile** : GT911 (I²C) avec calibration logicielle.
- **Wi‑Fi STA** : configuration SSID/mot de passe via interface LVGL (persisté NVS, reboot requis).
- **Client REST** : `/api/status`, `/api/light/dome0`, `/api/calibrate/uvb`, `/api/alarms/mute`, `/api/climate`.
- **Sûreté** : watchdog réseau, bannière d’erreur si API indisponible, feedback visuel sur interlocks UV.

---

## Build & Flash

- Prérequis : VSCode + **PlatformIO**.
- Carte : `esp32-s3-devkitc-1`.
- `sdkconfig.defaults` : OPI Flash/PSRAM @80 MHz activés, USB‑CDC, Dual‑OTA.
- Partitions (`partitions.csv`) : `factory + ota_0 + ota_1 + nvs + spiffs`.

```bash
# Contrôleur
cd firmware/controller
pio run -t upload -e s3-wroom2-idf

# Dôme
cd ../dome
pio run -t upload -e s3-wroom2-idf

# Panel tactile (ESP-IDF v5.1+)
cd ../panel
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

---

## Réseau & UI

- **AP+STA** : `SSID=terrarium-s3`, `PASS=terrarium123` (modifier dans `wifi.c` ou via NVS si tu l’ajoutes).  
- **UI Web** : `/` → sliders CCT/UVA/UVB, configuration UVB pulsé, capteurs, calibration UVI, mute alarmes.  
- **mDNS** (option si tu l’actives) : `http://terrarium-s3.local/`.

---

## API HTTP (REST)

### `/api/light/dome0`
- **GET** → état du dôme (CCT, UVA/UVB, pulsé, sky, `status`).  
- **POST** body JSON :

```json
{
  "cct": { "day": 0..10000, "warm": 0..10000 },
  "uva": { "set": 0..10000 },
  "uvb": { "set": 0..10000, "period_s": 5..600, "duty_pm": 0..10000 },
  "sky": 0|1|2
}
```

> **Clamp UVI** : côté contrôleur, `uvb.set` est **limité** pour ne pas dépasser `UVI_max` calibré. Le dôme a aussi ses clamps internes (seconde barrière).

### `/api/status` (GET)
Retourne un JSON agrégé **capteurs + dôme** :
```json
{
  "sensors": {
    "ds18b20_bus1_c": 25.31,
    "ds18b20_bus1_present": true,
    "ds18b20_bus2_c": 26.02,
    "ds18b20_bus2_present": true,
    "sht31_t_c": 25.7, "sht31_rh": 55.2, "sht31_present": true,
    "sht21_t_c": 25.6, "sht21_rh": 55.1, "sht21_present": true,
    "bme280_t_c": 25.8, "bme280_rh": 54.9, "bme280_p_hpa": 1009.3, "bme280_present": true
  },
  "dome": {
    "status": 0,
    "interlock": false,
    "therm_hard": false,
    "ot_soft": false,
    "bus_loss_degraded": false,
    "t_heatsink_c": 34
  }
}
```

### `/api/calibrate/uvb`  
- **GET** → `{"k": <UVI/‰>, "uvi_max": <float>}`.  
- **POST** → `{"duty_pm": <float>, "uvi": <float>, "uvi_max": <float>}`.  
  - `k = UVI / duty_pm` (modèle linéaire), stocké en **NVS** (`namespace "calib"`).  
  - `uvi_max` (clamp cible) stocké également.

### `/api/alarms/mute`
- **GET** → `{"muted": true|false}` (état persistant NVS).
- **POST** → `{"muted": true|false}`.
- **Réarmement physique** : appui long **> 2 s** sur le bouton → **unmute** + clear **BUS_LOSS** (3 bips d’ack).

### `/api/climate`
- **GET** → `{ "schedule": {...}, "state": {...}, "measurement": {...} }`.
- **POST** → met à jour le `climate_schedule_t` (plages jour/nuit, consignes T/RH, cibles UVI). Validation stricte (bornes dans `drivers/climate.c`).
- Les dérives et états SSR/FAN sont aussi exposés dans `/api/status.climate` pour supervision rapide.

---

## Régulation climatique (contrôleur)

- `drivers/climate.c` gère un `climate_schedule_t` persistant (NVS `namespace "climate"`) avec profils jour/nuit (consignes T/RH, hystérésis, UVI max).
- `climate_tick()` pilote SSR chauffage, ventilateurs PWM et clamp UVB en fonction des mesures `terra_sensors_t` et de l’heure RTC (`ds3231`).
- `climate_measurement_t` capture dérives et tendances ; accès thread-safe via `climate_measurement_mutex()`.
- `/api/climate` valide les consignes (bornes contrôlées) puis les sérialise en JSON NVS ; reboot inutile.
- En mode dégradé (BUS I²C perdu, capteur absent), SSR chauffage est forcé OFF et les alarmes sont propagées à l’UI et au panel.

---

## Dôme — Registres I²C (esclave @ 0x3A)

| Reg | Nom                 | Taille | Description                                                                 |
|-----|---------------------|--------|-----------------------------------------------------------------------------|
| 0x00| **STATUS**          | 1      | Bits: `OT(0)`, `UVA_LIMIT(1)`, `UVB_LIMIT(2)`, `FAN_FAIL(3)`, `BUS_LOSS(4)`, `INTERLOCK(5)`, `THERM_HARD(6)` |
| 0x01| MODE                | 1      | `ON(0)`, `SKY(1)`, `LOCK(7)` (réservé baseline)                             |
|0x02–0x03| CCT1 (Day)     | 2 LE   | **permille** 0..10000                                                       |
|0x04–0x05| CCT2 (Warm)    | 2 LE   | **permille** 0..10000                                                       |
| 0x06| UVA_SET             | 1      | 0..100 → converti en ‰ (×100)                                               |
| 0x07| UVB_SET             | 1      | 0..100 → converti en ‰ (×100)                                               |
| 0x08| SKY_CFG             | 1      | 0=off, 1=blue, 2=twinkle                                                    |
| 0x09| UVA_CLAMP           | 1      | max 0..100 → ‰ (×100)                                                       |
| 0x0A| UVB_CLAMP           | 1      | max **permille** 0..10000                                                   |
| 0x0B| UVB_PERIOD_S        | 1      | période pulsé (s)                                                           |
| 0x0C| UVB_DUTY_PM         | 1      | **permille** 0..10000 (résolution 1‰)                                       |
| 0x20| TLM_T_HEAT (°C)     | 1      | Temp. dissipateur arrondie                                                  |

**Protocole** : écriture `[REG][LEN][payload...]` ; lecture `[REG]` → renvoie **1 octet**.

---

## Routage I²C via **TCA9548A**

- Adresse `0x70`.  
- Masques : `Dôme → 0x01`, `Capteurs → 0x02`.  
- Le contrôleur sélectionne le canal **avant** chaque transaction (`drivers/dome_bus.c` et `drivers/sensors.c`).

---

## Capteurs supportés

- **DS18B20** (1‑Wire, bus1/bus2 indépendants).  
- **SHT31** (`0x44`) et **SHT21/HTU21** (`0x40`). Les deux peuvent coexister ; champs séparés dans `/api/status`.  
- **BME280** (`0x76`) — implémentation de lecture **simplifiée** (T/RH/Pression).
- **GT911** (panel tactile) — contrôlé via `components/drivers/touch_gt911`.

---

## Sécurité & Failsafe

- **Interlock capot** (GPIO17, actif bas) : **UV OFF < 100 ms** (ISR + boucle 50 ms) + `STATUS.INTERLOCK`.  
- **Thermostat hard 85–90 °C** en **série** CH1–CH4 → coupure physique (option readback `ST_THERM_HARD`).  
- **OT soft 75 °C** : fade UV→OFF, `STATUS.OT`.  
- **BUS_LOSS watchdog** (contrôleur ↔ dôme) : mode **dégradé** après > 5 erreurs I²C ; **auto‑reset** après ≥ 3 lectures OK ; **buzzer** pattern dédié ; exposé dans `/api/status`.  
- **Mute alarmes** persistant (NVS) + **réarmement** bouton (> 2 s).
- **Régulation climatique** : si `dome_bus_is_degraded()` ou capteurs absents, SSR chauffage passe OFF et le panel affiche l’alarme.

---

## Calibration **UVI** (procédure)

1. Placer un **radiomètre UVI** au **point de mesure** (ex. 30 cm sous le dôme).  
2. Depuis l’UI, régler `UVB duty (‰)` (ex. 1000 ‰ = 10 %).  
3. Lire le **UVI mesuré** → envoyer `POST /api/calibrate/uvb` avec `{"duty_pm":1000, "uvi":<mesure>, "uvi_max":<cible>}`.  
4. Répéter à 2–3 niveaux pour vérifier **linéarité**. Si non linéaire, remplacer le modèle linéaire par une **LUT** / polynôme (module `calib.c`).  
5. `uvb.set` côté API sera **clampé** pour ne jamais dépasser `UVI_max`.

---

## CI/CD

- **GitHub Actions** : build `controller`, `dome` **et panel** (artefacts `.bin` + `.uf2`).
- Workflow : `.github/workflows/build.yml`.

---

## Limitations (connues)

- BME280 : compensation pression simplifiée (suffisant pour T/RH usage terra).  
- Map registres du dôme minimaliste (1 octet/lecture). Étendre si besoin (blocs multi‑octets).  
- Pas de TLS/HTTPS (ajouter esp‑tls + certs si nécessaire).

---

## Roadmap (suggestions)

- LUT UVI (non‑linéarités), profils espèces (UVI/photopériodes).
- Mode RTC offline + schedules en NVS.
- Web UI : presets, graphiques temps réel, OTA via UI.
- CEM : blindage, plans de masse soignés, tests normatifs.
- Panel : multi-profils d’affichage, mode dégradé offline.

---

## Licence

À définir par le porteur du projet (MIT/BSD/GPL… selon préférence).

---

## Changelog

- **v0.4.0** : Panel LVGL ESP32‑S3 (LVGL 9, GT911, client REST), régulation climatique complète (`/api/climate`, NVS), documentation climatisation & validation mise à jour, CI étendue au panel.
- **v0.3.3** : Mute alarmes (NVS), API `/api/alarms/mute`, **long‑press** bouton → clear BUS_LOSS + unmute.
- **v0.3.2** : Buzzer patterns, watchdog BUS_LOSS refactor, capteurs explicités.
- **v0.3.1** : SHT21/SHT21, `/api/status` enrichi, wrappers I²C avec mode dégradé.  
- **v0.3.0** : Interlock capot < 100 ms, `/api/status`, calibration UVI (NVS), TCA routing, sécurité dôme.  
- **v0.2** : API `/api/light/dome0`, UI sliders, UVB pulsé, WS2812 RMT, capteurs basiques.  
- **v0.1** : Squelettes PlatformIO/ESP‑IDF (contrôleur + dôme), I²C maître/esclave, HTTP minimal.

---

### Avertissement

Ce dépôt inclut des fonctionnalités **UV**. L’implémenteur est **responsable** de la conformité (EN/IEC 62471, LVD, CEM) et de la **sécurité des animaux et des personnes**.
