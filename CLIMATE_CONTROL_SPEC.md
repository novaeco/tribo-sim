# 🌡️ Climate Control Panel - ESP32-P4 7" (JC1060P470C)

## 📋 Spécification du Projet

### Objectif
Système de gestion climatique pour terrariums avec simulation temps réel.
Panneau de contrôle intelligent sur écran 7" 1024x600 avec ESP32-P4 + ESP32-C6.

---

## 🔧 Hardware

### Carte principale
- **Modèle**: GUITION JC1060P470C_I_W_Y
- **MCU Principal**: ESP32-P4NRW32 (RISC-V dual-core 400MHz)
- **Co-processeur**: ESP32-C6-MINI-1U-N4 (WiFi 6 + BLE 5)
- **Écran**: 7" IPS TFT 1024x600 (JD9165BA controller)
- **Touch**: Capacitif GT911
- **PSRAM**: 32MB
- **Flash**: 16MB
- **SD Card**: Slot MicroSD

### Différences avec le 4.3"
| Caractéristique | 4.3" (JC4880P443C) | 7" (JC1060P470C) |
|-----------------|--------------------|--------------------|
| Résolution | 480x800 | 1024x600 |
| LCD Controller | ST7701 | JD9165BA |
| SD Mode | SPI | SDMMC 4-bit |
| Orientation | Portrait | Paysage |

---

## 🌍 Types de Terrariums Simulés

### 1. 🏜️ Désertique
- **Température jour**: 35-40°C (point chaud 45°C)
- **Température nuit**: 20-25°C
- **Humidité**: 20-30%
- **Éclairage**: 12-14h UV fort (Zone 4 Ferguson)
- **Exemples**: Pogona, Uromastyx, varanidae

### 2. 🌵 Semi-Désertique
- **Température jour**: 28-35°C (point chaud 38°C)
- **Température nuit**: 18-22°C
- **Humidité**: 30-50%
- **Éclairage**: 10-12h UV moyen (Zone 3 Ferguson)
- **Exemples**: Python royal, Serpent des blés, varanidae

### 3. 🌴 Tropical
- **Température jour**: 26-30°C
- **Température nuit**: 22-26°C
- **Humidité**: 70-90%
- **Éclairage**: 10-12h UV faible (Zone 2 Ferguson)
- **Brumisation**: 3-5x/jour
- **Exemples**: Python vert, Dendrobates, Caméléon

### 4. 🌿 Semi-Tropical
- **Température jour**: 24-28°C
- **Température nuit**: 20-24°C
- **Humidité**: 50-70%
- **Éclairage**: 10-12h UV moyen (Zone 1 Ferguson)
- **Brumisation**: 1-2x/jour
- **Exemples**: Boa, Morelia, Gecko à crête

---

## 📐 Zones de Ferguson (UV Index)

Classification UV pour reptiles selon Dr. Gary Ferguson:

| Zone | UVI Range | Description | Exemples |
|------|-----------|-------------|----------|
| **Zone 1** | 0.0 - 0.7 | Crépusculaire/Ombre | Geckos nocturnes, serpents |
| **Zone 2** | 0.7 - 1.0 | Ombre partielle | Boa, Python |
| **Zone 3** | 1.0 - 2.6 | Soleil filtré | Caméléon, Pogona (ombre) |
| **Zone 4** | 2.6 - 3.5 | Plein soleil | Pogona, Uromastyx |

---

## 🎛️ Équipements Simulés par Terrarium

### Capteurs (Simulation)
- **2x Sondes température**: Zone chaude + Zone froide
- **1x Sonde humidité**: Ambiante
- **1x Capteur niveau eau**: Bassin
- **1x Capteur niveau eau**: Réservoir brumisation

### Actionneurs (Simulation)
- **1x Chauffage principal**: Tapis/Céramique (ON/OFF ou %)
- **1x Lampe UV**: Selon zone Ferguson (0-100%)
- **1x Éclairage jour/nuit**: LED (ON/OFF programmable)
- **1x Brumisateur**: Électrovanne (cycles programmés)
- **1x Pompe cascade**: (ON/OFF programmable)

---

## 📊 Interface Utilisateur (Pages LVGL)

### Page 1: 🏠 Dashboard Principal
- Vue d'ensemble de tous les terrariums (cards)
- Indicateurs rapides: T°/Humidité/Alertes
- Bouton accès rapide par terrarium

### Page 2: 🦎 Vue Terrarium Détaillée
- Sélection du terrarium actif
- Graphique température temps réel (2 zones)
- Graphique humidité
- Status de tous les équipements
- Boutons ON/OFF manuels

### Page 3: ⏰ Programmation Horaire
- Timeline 24h visuelle
- Programmation par équipement:
  - Éclairage UV (lever/coucher)
  - Chauffage (jour/nuit)
  - Brumisation (cycles)
  - Cascade (horaires)

### Page 4: 🌡️ Zones Ferguson & UV
- Configuration zone UV par terrarium
- Slider intensité UV
- Aide visuelle avec recommandations espèces

### Page 5: 💧 Gestion Eau
- Niveaux bassins (jauges visuelles)
- Niveaux réservoirs brumisation
- Alertes niveau bas
- Historique consommation

### Page 6: ⚠️ Alertes & Historique
- Liste des alertes actives
- Historique des événements
- Configuration seuils d'alerte
- Notifications (température hors plage, niveau eau bas)

### Page 7: ⚙️ Paramètres
- Configuration WiFi/Bluetooth
- Mode communication (WiFi/BLE/ESP-NOW)
- Synchronisation avec panel 4.3"
- Paramètres système

---

## 📡 Communication avec Panel 4.3"

### Options de communication:

#### 1. WiFi (TCP/UDP)
```
Panel 7" (Climate) <--WiFi--> Panel 4.3" (Reptiles)
                          |
                     Réseau local
```
- Avantage: Distance illimitée sur même réseau
- Format: JSON ou Protocol Buffers

#### 2. Bluetooth Low Energy
```
Panel 7" (Climate) <--BLE--> Panel 4.3" (Reptiles)
```
- Avantage: Pas besoin de réseau WiFi
- Portée: ~10-30m
- Mode: GATT Client/Server

#### 3. ESP-NOW
```
Panel 7" (Climate) <--ESP-NOW--> Panel 4.3" (Reptiles)
```
- Avantage: Faible latence, pas d'infrastructure
- Portée: ~200m (ligne de vue)
- Mode: Peer-to-peer

### Données échangées:
```json
{
  "terrarium_id": 1,
  "timestamp": 1704556800,
  "climate": {
    "temp_hot": 35.2,
    "temp_cold": 26.8,
    "humidity": 45,
    "uv_level": 2.5
  },
  "equipment": {
    "heating": true,
    "uv_lamp": 80,
    "misting": false,
    "pump": true
  },
  "alerts": ["TEMP_HIGH"]
}
```

---

## 🗂️ Structure des Données

### Terrarium Configuration
```c
typedef struct {
    uint8_t id;
    char name[32];
    terrarium_type_t type;      // DESERT, SEMI_DESERT, TROPICAL, SEMI_TROPICAL
    ferguson_zone_t uv_zone;    // ZONE_1 to ZONE_4
    
    // Temperature targets
    float temp_day_min;
    float temp_day_max;
    float temp_night_min;
    float temp_night_max;
    float temp_hot_spot;
    
    // Humidity targets
    uint8_t humidity_min;
    uint8_t humidity_max;
    
    // Schedules
    uint8_t light_on_hour;      // 0-23
    uint8_t light_off_hour;
    uint8_t mist_interval_min;  // Minutes between misting
    uint8_t mist_duration_sec;  // Duration of each mist
} terrarium_config_t;
```

### Sensor Readings (Simulated)
```c
typedef struct {
    float temp_hot_zone;
    float temp_cold_zone;
    float humidity;
    float uv_index;
    uint8_t water_basin_level;      // 0-100%
    uint8_t water_reservoir_level;  // 0-100%
} sensor_data_t;
```

### Equipment State
```c
typedef struct {
    bool heating_on;
    uint8_t heating_power;      // 0-100%
    bool uv_lamp_on;
    uint8_t uv_intensity;       // 0-100%
    bool day_light_on;
    bool misting_on;
    bool pump_on;
} equipment_state_t;
```

---

## 🔄 Simulation Temps Réel

### Logique de simulation:

1. **Température**:
   - Varie selon heure (jour/nuit)
   - Influence du chauffage (+0.5°C/min quand ON)
   - Refroidissement naturel (-0.1°C/min quand OFF)
   - Bruit aléatoire (±0.2°C)

2. **Humidité**:
   - Augmente avec brumisation (+5%/cycle)
   - Diminue naturellement (-1%/5min)
   - Influence de la température (plus chaud = plus sec)

3. **Niveaux d'eau**:
   - Bassin: diminue lentement (-0.5%/heure)
   - Réservoir: diminue avec brumisation (-2%/cycle)

4. **Alertes automatiques**:
   - Température > max + 3°C → ALERTE HAUTE
   - Température < min - 3°C → ALERTE BASSE
   - Niveau eau < 20% → ALERTE NIVEAU BAS

---

## 📁 Structure Fichiers Projet

```
esp32p4_7inch_panel/
├── main/
│   ├── main.c                      # Point d'entrée
│   ├── climate_manager.c           # Logique simulation
│   ├── climate_manager.h
│   ├── ui_climate.c                # Pages LVGL climat
│   ├── ui_climate.h
│   ├── terrarium_presets.c         # Configs prédéfinies
│   ├── terrarium_presets.h
│   ├── comm_protocol.c             # WiFi/BLE/ESP-NOW
│   ├── comm_protocol.h
│   └── ...
├── components/
│   └── (si nécessaire)
├── sdkconfig.defaults
└── README.md
```

---

## ✅ Checklist Implémentation

### Phase 1: Nettoyage
- [ ] Supprimer code gestion reptiles du 7"
- [ ] Adapter thème couleurs (bleu/cyan pour climat)
- [ ] Vérifier compilation

### Phase 2: Simulation Core
- [ ] Structures de données terrarium
- [ ] Presets par type (désert, tropical, etc.)
- [ ] Moteur de simulation temps réel
- [ ] Système d'alertes

### Phase 3: Interface LVGL
- [ ] Dashboard principal
- [ ] Vue terrarium détaillée
- [ ] Page programmation horaire
- [ ] Page zones Ferguson
- [ ] Page gestion eau
- [ ] Page alertes

### Phase 4: Communication
- [ ] Implémentation WiFi JSON
- [ ] Implémentation BLE GATT
- [ ] Implémentation ESP-NOW
- [ ] Sélection mode dans UI

### Phase 5: Polish
- [ ] Animations transitions
- [ ] Sauvegarde NVS
- [ ] Logs sur SD
- [ ] Tests complets

---

**Date de création**: 2026-01-06
**Version**: 1.0
**Auteur**: AI Assistant
