# Notes de Migration - Tribo-Sim avec Configuration ESP-IDF 6.1

## Résumé des Changements

Ce projet a été mis à jour avec la configuration matérielle fonctionnelle provenant du projet `tribo-sim-7pouce`. Les modifications suivantes ont été appliquées:

### 1. Configuration ESP-IDF mise à jour (v6.1)

- **sdkconfig.defaults** - Mis à jour avec les paramètres optimisés pour ESP32-P4 rev 1.3
- **CMakeLists.txt** - Ajout des workarounds pour GCC 15.x et LVGL 9.4
- Support complet pour écran 7" 1024x600 (JD9165)
- Configuration PSRAM optimisée (32MB à 200MHz)
- Watchdog désactivé pour éviter les crashes pendant les opérations WiFi+BLE+LVGL

### 2. Configuration Matérielle

| Composant | Configuration |
|-----------|--------------|
| Écran | JD9165 7" 1024x600 MIPI-DSI (2 lanes @ 800Mbps) |
| Touch | GT911 I2C (SDA:10, SCL:11, INT:12, RST:13) |
| LCD Reset | GPIO 38 |
| LCD Backlight | GPIO 39 |
| PSRAM | 32MB OCT @ 200MHz |
| CPU | ESP32-P4 @ 400MHz |

### 3. Composants Préservés

Tous les composants de jeu du projet original ont été conservés:
- ✅ `components/game/` - Logique de simulation tamagotchi
- ✅ `components/anim/` - Système d'animation
- ✅ `components/storage/` - Persistance des données
- ✅ `components/sim_display/` - Driver écran + UI LVGL
- ✅ `components/input/` - Driver tactile GT911
- ✅ `components/audio/` - Audio buzzer
- ✅ `components/ota/` - Support OTA

### 4. Table de Partitions

La table de partitions inclut:
- NVS (24KB)
- OTA Data (8KB)
- OTA_0 (3MB) - Premier slot OTA
- OTA_1 (3MB) - Second slot OTA
- SPIFFS (1MB) - Stockage des sauvegardes et assets

## Prochaines Étapes

### 1. Setup ESP-IDF 6.1

```bash
# Cloner ESP-IDF 6.1
git clone -b release/v6.1 --recursive https://github.com/espressif/esp-idf.git ~/esp-idf-6.1

# Installer les dépendances
cd ~/esp-idf-6.1
./install.sh esp32p4

# Activer l'environnement
source ~/esp-idf-6.1/export.sh
```

### 2. Build du Projet

```bash
cd tribo-sim-github

# Définir la cible
idf.py set-target esp32p4

# Build
idf.py build
```

### 3. Flash sur ESP32-P4

```bash
# Flash complet (bootloader + partitions + app)
idf.py -p COMx flash

# Monitoring
idf.py -p COMx monitor
```

## Configuration Personnalisée

Si vous devez modifier la configuration:

```bash
idf.py menuconfig
```

Options importantes:
- **Component config → LVGL configuration** - Paramètres LVGL
- **Display Configuration** - GPIOs de l'écran
- **Touch Configuration** - GPIOs et I2C du GT911
- **Game Configuration** - Intervalle de mise à jour

## Dépendances

Le projet utilise `idf_component.yml` pour gérer les dépendances:
- LVGL 9.4.0 (via ESP Component Registry)

Les dépendances sont automatiquement téléchargées lors du premier build.

## Problèmes Connus

### Avertissements de Compilation

Les workarounds suivants sont déjà en place dans `CMakeLists.txt`:
```cmake
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=attributes" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=cpp" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)
```

### Watchdog Désactivé

Pour éviter les resets pendant les opérations lourdes (WiFi + LVGL), le watchdog est désactivé dans `sdkconfig.defaults`:
```
CONFIG_ESP_TASK_WDT_INIT=n
CONFIG_ESP_TASK_WDT_EN=n
```

En production, réactivez le watchdog avec un timeout plus long (30s recommandé).

## Test de Fonctionnement

Après le flash, vous devriez voir:
1. Écran s'allume (backlight GPIO 39)
2. UI LVGL s'affiche sur l'écran 1024x600
3. Écran d'accueil avec bouton "Commencer"
4. Simulation tamagotchi démarre après appui sur le bouton

## Support

Pour toute question ou problème:
- Vérifier les logs via `idf.py monitor`
- Consulter la documentation ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/v6.1/
- GitHub Issues: https://github.com/novaeco/tribo-sim/issues

## Compatibilité

- ✅ ESP-IDF 6.1+
- ✅ ESP32-P4 rev 1.x (< v3.0)
- ✅ LVGL 9.4.0
- ✅ Écran JD9165 7" 1024x600
- ✅ Touch GT911
