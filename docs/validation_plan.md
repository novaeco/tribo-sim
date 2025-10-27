# Plan de validation — Terrarium S3 v0.3

## 1) Sécurité hard (dôme)
- **Thermostat 85–90 °C** en série CH1–CH4 : vérifier coupure de courant LED CC (alimentation stable, aucun PWM résiduel).
- **Interlock capot** (GPIO17, actif bas) :
  - Mesurer temps OFF UV : < **100 ms** (oscilloscope sur CH3/CH4).
  - Vérifier flag `ST_INTERLOCK` dans `/api/light/dome0` (status) et INT tiré bas.

## 2) Capteurs terra (/api/status)
- DS18B20 (bus1/bus2), SHT31, BME280 : vérifier valeurs plausibles, latence API < 150 ms.
- Tests perte capteur : débrancher SHT31/BME280 ⇒ aucune panne système, valeurs nulles/NaN traitées.

## 3) TCA9548A
- Router Dôme sur canal 0x01, Capteurs sur 0x02.
- Sniffer I²C (analyseur logique) : vérifier `0x70` écrit avec masques attendus avant transactions.

## 4) Calibration UVB
- **POST /api/calibrate/uvb** avec `{duty_pm, uvi, uvi_max}` :
  - Confirmer stockage NVS (reboot → **GET** renvoie mêmes `k` et `uvi_max`).
  - Vérifier clamp : requête UVB set > autorisé ⇒ écriture ramenée à `UVI_max` (calculée).

## 5) Endurance & CEM
- **Burn-in 100 h** : cycles jour/nuit, UVB pulsé 60s/10% ; monitorer T° dissipateur, erreurs.
- **BUS_LOSS I²C** : débrancher dôme → contrôleur doit continuer (log warning), ré-brancher → reprise OK.
- **72 h** stress : Wi‑Fi + HTTP + OTA dummy, logs continus.
- **Pré‑CEM** : émissions conduites/rayonnées (antenne TEM), ESD ±8 kV air ; corriger si nécessaire.

## 6) Acceptation
- Interlock < 100 ms, OT soft 75 °C (UV off), hard 85–90 °C (coupure).
- `/api/status` complet (T/RH/Pression + T dissipateur dôme).
- Limitation UVB par **UVI** respectée à ±10 % (radiomètre).

