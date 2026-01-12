# REPTILE SIM: Professional Breeding Simulator

**Type:** Hardcore Professional Simulator / Serious Game / Zootechnical Tool
**Hardware:** ESP32-P4 (Display/UI) + ESP32-C6 (Network/Sim Offload)
**Objective:** Professional Training, Breeding Management, Research

---

## Overview

More than a game, REPTILE SIM is a portable laboratory that simulates invisible mechanisms (genetics, bacteriology, physiology) to train elite breeders. It combines scientific accuracy with pedagogical gameplay.

### Key Features

- **Connected:** Real weather impacts virtual breeding
- **Scientific:** Inbreeding calculations, nutritional biochemistry
- **Educational:** Learning through consequence management
- **14 Simulation Engines:** Physics, Biology, Genetics, Economy, and more

---

## Hardware

| Component | Description |
|-----------|-------------|
| MCU | ESP32-P4 (Dual Core + LP Core, 400MHz) |
| Display | 7" 1024x600 MIPI-DSI (JD9165BA) |
| Touch | GT911 (I2C: GPIO7/GPIO8) |
| WiFi | ESP32-C6 co-processor (Weather API) |
| Storage | MicroSD (SDMMC Slot 0) for saves |

---

## The 14 Simulation Engines

### 1. **Physical Engine** (Environment)
- Thermal inertia, gradients, real heatwave impacts via Weather API
- Hydrometric saturation, condensation, real storm impacts
- Day/night cycle, dynamic UV spectrum

### 2. **Biological Engine** (Physiology)
- Thermosensitive metabolism
- Hydration/shedding with dysecdysis risk
- Bone density calculation (Ca + UV)
- Stress-induced immunosuppression

### 3. **Sanitary Engine** (Biosecurity)
- Manual transmission vectors (wash hands!)
- Critical bacterial load thresholds
- Mandatory quarantine protocols

### 4. **Nutritional Engine** (Biochemistry)
- Gut-loading: prey nutritional value varies
- Ca/P ratio: phosphorus excess inhibits calcium
- Supplementation risks: deficiency vs hypervitaminosis

### 5. **Behavioral Engine** (Welfare)
- Enrichment requirements
- Stereotypic behaviors (leaping, rubbing)

### 6. **Gynecological Engine** (Reproduction)
- Dystocia: life-threatening egg-laying complications
- Strict incubation temperature curves
- TSD: Temperature-dependent sex determination

### 7. **Genetic Engine** (Zootechnics)
- Inbreeding coefficient calculation (5 generations)
- Mendelian transmission of defects (Wobble, Kink)
- Phase/morph probability calculator

### 8. **Social Engine** (Interactions)
- Hierarchy: dominant/submissive resource access
- Inter/intra-specific predation risk

### 9. **Seasonal Engine** (Rhythms)
- Brumation: essential winter rest cycle
- Photoperiod: annual day-length variation

### 10. **Safety Engine** (Venomous Species)
- Security checklist before enclosure opening
- Antivenom stock expiration management

### 11. **Technical Engine** (Equipment)
- MTBF: programmed random failures
- Power outage crisis management

### 12. **Administrative Engine** (Legal)
- Inviolable police registry (IFAP/CDC)
- Automated compliance audits

### 13. **Dynamic Weather Engine** (Real World)
- API: retrieval of real local temperature/pressure
- Impact: player's heatwave = game overheating

### 14. **Economic Engine** (Management)
- ROI: breeding profitability calculation
- Costs: electricity, water, food, veterinary

---

## Game Modes

### A. Career Mode (Novice to Professional)
Slow progression. "Difficult" species unlock only after success with "Easy" species (proof of competence).

### B. Crisis Management Mode (Scenarios)
Pre-established scenarios: "You inherit a seized breeding facility, 30 sick animals, zero budget. Save them."

### C. Genetics Laboratory
Sandbox to test crosses and understand Mendel's laws without animal risk.

---

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

---

## Project Structure

```
reptile_sim_game/
├── main/
│   ├── main.c                    # Application entry point
│   ├── ui_game.c/h               # Game UI (LVGL)
│   ├── career_mode.c/h           # Career progression system
│   ├── scenario_manager.c/h      # Crisis scenarios
│   ├── genetics_lab.c/h          # Genetics sandbox
│   ├── registry_legal.c/h        # Legal registry (IFAP)
│   ├── save_system.c/h           # Save/load game state
│   ├── tutorial_system.c/h       # Tutorial system
│   ├── simulation/
│   │   ├── sim_engine.c/h        # Main simulation loop
│   │   ├── sim_physics.c/h       # Physical engine
│   │   ├── sim_biology.c/h       # Biological engine
│   │   ├── sim_nutrition.c/h     # Nutritional engine
│   │   ├── sim_sanitary.c/h      # Sanitary engine
│   │   ├── sim_genetics.c/h      # Genetic engine
│   │   ├── sim_reproduction.c/h  # Reproduction engine
│   │   ├── sim_behavior.c/h      # Behavioral engine
│   │   ├── sim_seasonal.c/h      # Seasonal engine
│   │   ├── sim_economy.c/h       # Economic engine
│   │   ├── sim_admin.c/h         # Administrative engine
│   │   └── sim_weather_api.c/h   # Weather API integration
│   └── slave_fw/                 # ESP32-C6 firmware
├── sdkconfig.defaults            # Default configuration
├── partitions.csv                # Partition table
├── DESIGN_SPEC.md                # Full game design document
└── CMakeLists.txt                # Project build file
```

---

## Configuration

Key settings in `app_config.h`:

| Setting | Value | Description |
|---------|-------|-------------|
| `MAX_REPTILES` | 50 | Maximum animals in game |
| `MAX_TERRARIUMS` | 10 | Maximum enclosures |
| `GAME_TIME_SCALE` | 60.0f | 1 real sec = 1 game min |
| `ENABLE_WEATHER_API` | true | Real weather integration |

---

## Interface (UI)

- **Dashboard:** Animal/terrarium monitoring
- **Registry:** Administrative management
- **Lab/Vet:** Care & autopsy
- **Genealogy:** Interactive phylogenetic tree
- **Crisis Scenarios:** Timed challenges

---

## Philosophy

**The Ultimate Tool.**

This isn't just entertainment - it's a portable laboratory simulating mechanisms invisible to the naked eye for professional training. Error is the only accepted learning method.

---

## License

Copyright (c) 2026. All rights reserved.

## Version History

- **v2.0** (2026-01-12): Full game transformation with 14 simulation engines
- **v1.0** (2026-01-08): Initial terrarium controller
