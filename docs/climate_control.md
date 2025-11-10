# Régulation climatique — Terrarium S3

## Objectif
Assurer un microclimat stable (température, hygrométrie, UV) avec profils jour/nuit programmables, hystérésis maîtrisée et commandes coordonnées des actionneurs (SSR chauffage/éclairage, ventilateurs PWM, dômes UV).

## Planification jour/nuit
La structure `climate_schedule_t` (persistée en NVS, clé `climate/schedule`) décrit deux profils :

| Champ | Description | Bornes |
|-------|-------------|--------|
| `day_start_min`, `night_start_min` | bascule minute UTC/RTC | 0–1439 |
| `day` / `night` → `temp_c` | consigne température | 5–45 °C |
| `…temp_hysteresis_c` | bande hystérésis | 0,1–10 °C |
| `…humidity_pct` | consigne hygrométrie | 5–100 % |
| `…humidity_hysteresis_pct` | bande hygrométrie | 0,1–10 % |
| `day_uvi_max` / `night_uvi_max` | cible UVI par phase | 0–20 |

`climate_tick()` active le profil correspondant à l'heure RTC (`ds3231`) ou, en secours, au `tick` FreeRTOS.

## Machine d’états (texte)
```
                          +---------------------+
                          |    NIGHT (lights 0) |
                          +----------+----------+
                                     |
                                     | sunrise (minute == day_start)
                                     v
+---------------------+   temp < set - hyst/2    +----------------------+
| DAY (lights 1)      |------------------------->| HEATING = SSR0 ON    |
| Fan base 25 %       |<-------------------------| Maintient jusqu'à    |
| UVI target > 0      |  temp > set + hyst/2     | temp > set + hyst/2 |
+----------+----------+                          +----------------------+
           |
           | sunset (minute == night_start)
           v
(reboucle sur NIGHT)
```
Les ventilateurs passent progressivement de 15/25 % (base) à 65 % ou 100 % selon dépassement hygrométrique (`> consigne + hyst` → purge maximale).

## Priorités failsafe
1. **Sécurité dôme** : interlocks matériels (surveillance `dome_bus_is_degraded`, flags `therm_hard`, `interlock`) restent prioritaires et peuvent bloquer `dome_bus_write` (SSR chauffage reste actif uniquement si bus sain).
2. **Thermique** : SSR0 coupé instantanément si `dome_bus_is_degraded()` ou si capteur principal absent (hystérésis désactivée → OFF).
3. **UV** : `allowed_uvi = min(schedule_uvi, calibration_uvi_max)` garantit que la consigne ne dépasse jamais la limite radiométrique validée (`/api/calibrate/uvb`).
4. **Hygrométrie** : ventilation augmente avant toute action lumière (évite condensation sur optiques).

## Tâches FreeRTOS
- `sensors_task` (période 2 s) : agrège `sensors_read()` (retourne un masque de défauts explicite), calcule dérives vs consigne et pousse `climate_measurement_t` (protégé par mutex `climate_measurement_mutex`).
- `actuators_task` (période 1 s) : exécute `climate_tick()`, pilote SSR/FAN/dôme, applique clamp UVI et journalise anomalies (`ESP_LOGW`).
- `btn_rearm_task` : existante, assure réarmement manuel BUS_LOSS + buzzer.

## API HTTP
`GET /api/climate` expose :
- `schedule` (config courante),
- `state` (profil actif, erreurs thermo/hygro, états SSR/FAN),
- `measurement` (timestamp + dérives).  
`POST /api/climate` accepte uniquement des charges valides (bornes ci-dessus) ; en cas de violation, renvoie `400`.

`GET /api/status` inclut désormais `climate` (états SSR/FAN + dérives) pour supervision rapide.

## Filtrage et santé capteurs

- `sensors_init()` configure chaque périphérique (DS18B20 ×2, SHT31, SHT21, BME280) une seule fois au boot : reset/chauffage OFF pour les SHT, oversampling normal pour le BME280.
- Le module conserve pour chaque capteur un contexte (`present`, `error`, timestamp milliseconde du dernier échantillon valide, dernier `esp_err_t`).
- Un filtrage configurable est appliqué avant `climate_tick()` : EMA (`alpha` par défaut 0,25) ou médiane glissante (fenêtre 3). Les valeurs filtrées sont exposées via `terra_sensors_t.temp_filtered_c` / `.humidity_filtered_pct` et privilégiées pour la régulation.
- Le masque de défauts (`sensor_fault_mask`) est propagé à `climate_measurement_t` et restitué dans `/api/status` (`sensor_fault_mask` + `sensor_status[]`).

## Procédure de mise à jour
1. Modifier le JSON via `/api/climate` avec les nouvelles consignes.
2. Vérifier `state.is_day` et dérives < hystérésis via GET.
3. Surveiller `measurement.temp_drift_c` (< ±0,5 °C en régime établi) pour confirmer stabilité.
