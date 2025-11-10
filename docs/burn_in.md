# Procédure de burn-in — Terrarium S3

Cette procédure valide l'endurance électrique, thermique et logicielle des ensembles contrôleur/dôme/panel avant expédition.

## 1. Préparation & instrumentation
- **Locaux** : enceinte ventilée fermée, capteurs UV homologués (IEC/EN 62471), thermocouples type K.
- **Alimentation** : source 24 V/10 A limitée en courant, alimentation USB-C 5 V isolée.
- **Instrumentation** : caméra thermique (≥ 320×240), oscilloscope 2 voies, enregistreur de consommation.
- **Firmware** : version de production signée, partitions OTA intactes, calibration UVI effectuée.
- **Outils** : `python tools/burn_in.py --host https://terrarium-s3.local --cycles 48 --period 600` (adapter host/IP).

## 2. Inspection visuelle
1. Contrôler la propreté PCB, absence de flux résiduel, couples de serrage.
2. Vérifier polarité connecteurs, torons, interlock mécanique.
3. Confirmer présence TVS/ferrites selon `docs/hardware_validation.md`.

## 3. Séquence d'endurance (48 h minimum)
| Phase | Durée | Action | Attendus |
|-------|-------|--------|----------|
| Montée en température | 2 h | CCT=6000 ‰, UVA=0, UVB duty=0 | Stabilisation dissipateur < 55 °C |
| Cyclage lumineux | 36 h | Script `burn_in.py` : bascule profils jour/nuit toutes les 10 min (CCT/UVA/UVB, sky) | Pas d'alarmes `ST_OT`, `ST_UVB_LIMIT`, `ST_INTERLOCK` |
| Stress climatique | 8 h | Forcer humidité 80 % RH, température ambiante 30 °C | Régulation climate maintient consignes ±1.5 °C / ±8 % RH |
| Final | 2 h | UVB duty 40 %, ventilation 80 %, vérifier tachy | Aucun jitter > 5 % sur PWM, tachymètre OK |

## 4. Surveillance & enregistrements
- **Logs** : `idf.py monitor` (contrôleur) + capture `/api/status` toutes les 10 min (script).
- **Température** : caméras sur dissipateur, buck, MCU ; alarme si > 75 °C.
- **UVI** : radiomètre aligné ; dérive admissible ±0.2 UVI.
- **I²C** : compter erreurs `DOME_REG_DIAG_I2C_ERRORS` < 5/h.
- **Wi-Fi** : ping panel ↔ contrôleur (1 Hz) ; perte < 0.5 %.

## 5. Tests fonctionnels intermédiaires (toutes les 12 h)
- Déclencher interlock capot → UV OFF < 100 ms, alarme rétablie en < 5 s.
- Lancer `/api/ota/status` → vérifier disponibilité partitions `ota_0`/`ota_1`.
- Consulter `/api/species` → confirmer profil appliqué.
- Panel : navigation écran Historique, présence courbes T/RH/UVI.

## 6. Clôture & rapport
1. Exporter mesures (`status_log.csv`, images thermiques, traces alimentation).
2. Documenter écarts et actions correctives.
3. Remplir fiche `docs/templates/burn_in_report.md` (créer si absente) : opérateur, dates, numéro de série.
4. Archiver sur serveur qualité (ISO 9001) pour traçabilité ≥ 10 ans.

## 7. Critères d'acceptation
- Aucun redémarrage hors plan, pas d'erreur FATFS/NVS.
- UVI, température, humidité dans tolérances pendant ≥ 95 % du temps.
- I²C : `BUS_LOSS` jamais déclenché, erreurs cumulées < 100 sur la campagne.
- Interlock fonctionnel, thermostat hard non déclenché.

## 8. Échec & reprise
- En cas d'alarme critique, consigner l'heure, isoler l'unité, diagnostiquer selon `docs/validation_plan.md`.
- Après correction, redémarrer la campagne complète (48 h) pour valider la réparation.
