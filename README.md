# Tribo-Sim - Simulateur de Terrarium pour Reptile

Simulation interactive de terrarium pour reptile sur ESP32-P4 avec écran tactile 7".

## Description

Tribo-Sim est un simulateur de type "Tamagotchi" pour reptile qui permet de gérer virtuellement un animal de compagnie dans son terrarium. L'application gère:

- **Santé** - État de santé général du reptile (0-100)
- **Faim** - Niveau de faim (0 = repu, 100 = affamé)
- **Température** - Contrôle du chauffage du terrarium
- **Croissance** - Progression de l'animal

## Hardware Requis

| Composant | Modèle | Spécifications |
|-----------|--------|----------------|
| MCU | ESP32-P4 | Dual-core, PSRAM |
| Écran | JD9165 | 7" LCD MIPI-DSI 1024x600 |
| Tactile | GT911 | Contrôleur I2C capacitif |

### Brochage par défaut

| Fonction | GPIO |
|----------|------|
| LCD Reset | 38 |
| LCD Backlight | 39 |
| Touch SDA | 10 |
| Touch SCL | 11 |
| Touch INT | 12 |
| Touch RST | 13 |

## Prérequis

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/) v5.3 ou supérieur
- Python 3.8+
- CMake 3.16+

## Installation

### 1. Cloner le dépôt

```bash
git clone https://github.com/novaeco/tribo-sim.git
cd tribo-sim
```

### 2. Configurer ESP-IDF

```bash
# Si ESP-IDF n'est pas déjà installé
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
source export.sh
cd ..
```

### 3. Configurer le projet (optionnel)

```bash
idf.py menuconfig
```

Les paramètres par défaut sont définis dans `sdkconfig.defaults`.

## Build

```bash
idf.py build
```

Le build génère les fichiers suivants dans `build/`:
- `reptile_sim.bin` - Firmware principal
- `bootloader/bootloader.bin` - Bootloader
- `partition_table/partition-table.bin` - Table de partitions

## Flash

### Connecter la carte ESP32-P4 via USB et flasher:

```bash
idf.py -p /dev/ttyUSB0 flash
```

Sur Windows, remplacer `/dev/ttyUSB0` par le port COM approprié (ex: `COM3`).

### Flash avec monitoring:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Appuyer sur `Ctrl+]` pour quitter le moniteur.

## Structure du Projet

```
tribo-sim/
├── CMakeLists.txt          # Configuration CMake principale
├── idf_component.yml       # Dépendances ESP-IDF (LVGL)
├── sdkconfig.defaults      # Configuration ESP-IDF par défaut
├── partitions.csv          # Table de partitions (SPIFFS)
├── README.md
├── main/
│   ├── CMakeLists.txt
│   ├── main.c              # Point d'entrée, orchestration FreeRTOS
│   └── Kconfig.projbuild   # Options de configuration
├── components/
│   ├── sim_display/        # Écran LCD JD9165 + UI LVGL
│   ├── input/              # Contrôleur tactile GT911
│   ├── game/               # Logique de simulation
│   ├── anim/               # Animations du reptile
│   ├── storage/            # Persistance SPIFFS/SD
│   └── esp_lcd_touch_gt911/# Driver GT911 personnalisé
└── document/               # Documentation hardware
```

## Architecture Logicielle

Le projet utilise FreeRTOS avec 4 tâches distribuées sur les 2 cœurs:

| Tâche | Core | Priorité | Fonction |
|-------|------|----------|----------|
| Display | 0 | 5 | Rendu LVGL, rafraîchissement écran |
| Input | 0 | 4 | Lecture tactile GT911 |
| Game | 1 | 3 | Logique de simulation |
| Anim | 1 | 2 | Animation du sprite |

## Configuration

### Via menuconfig

```bash
idf.py menuconfig
```

Options disponibles:
- **Display Configuration** - GPIO LCD
- **Touch Configuration** - GPIO et port I2C du GT911
- **Game Tuning** - Intervalle de mise à jour (ms)
- **Storage** - SPIFFS ou carte SD

### Options principales

| Option | Défaut | Description |
|--------|--------|-------------|
| `CONFIG_GAME_TICK_MS` | 1000 | Intervalle simulation (ms) |
| `CONFIG_USE_SD_CARD` | n | Utiliser SD au lieu de SPIFFS |
| `CONFIG_LCD_RESET_GPIO` | 38 | GPIO reset écran |
| `CONFIG_TOUCH_I2C_PORT` | 0 | Port I2C tactile |

## Dépendances

- **LVGL** v9.4.0 - Bibliothèque graphique
- **ESP-IDF** v5.3+ - Framework Espressif
- **FreeRTOS** - Système temps réel (inclus dans ESP-IDF)

## Commandes Utiles

```bash
# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash

# Monitoring série
idf.py -p /dev/ttyUSB0 monitor

# Build + Flash + Monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Nettoyer le build
idf.py fullclean

# Effacer la flash
idf.py -p /dev/ttyUSB0 erase-flash

# Créer partition SPIFFS
idf.py -p /dev/ttyUSB0 storage-flash
```

## Dépannage

### Le GT911 n'est pas détecté

1. Vérifier les connexions I2C (SDA, SCL)
2. Vérifier que les résistances de pull-up sont présentes
3. Le GT911 utilise l'adresse 0x14 ou 0x5D selon l'état de la broche INT au boot

### L'écran reste noir

1. Vérifier la connexion MIPI-DSI
2. Vérifier le GPIO de reset (38 par défaut)
3. Vérifier l'alimentation du rétroéclairage (GPIO 39)

### Build échoue avec "PSRAM not found"

Assurez-vous que votre carte ESP32-P4 dispose de PSRAM et que les options sont activées dans `sdkconfig.defaults`.

## CI/CD

Le projet utilise GitHub Actions pour le build automatique. Chaque push déclenche:

1. **Build** - Compilation avec ESP-IDF
2. **Lint** - Analyse statique avec cppcheck

Les artefacts (firmware .bin) sont disponibles dans les artifacts de chaque run.

## Licence

MIT License - Voir [LICENSE](LICENSE) pour plus de détails.

## Contribution

1. Fork le projet
2. Créer une branche feature (`git checkout -b feature/ma-feature`)
3. Commit les changements (`git commit -m 'Ajouter ma feature'`)
4. Push sur la branche (`git push origin feature/ma-feature`)
5. Ouvrir une Pull Request
