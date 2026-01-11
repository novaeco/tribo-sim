/**
 * @file climate_presets.h
 * @brief Presets de configuration pour les différents types de terrariums
 * @version 1.1
 * @date 2026-01-08
 *
 * Configurations prédéfinies basées sur les besoins des espèces:
 * - Désertique (Zone 4 Ferguson)
 * - Semi-Désertique (Zone 3 Ferguson)
 * - Tropical (Zone 2 Ferguson)
 * - Semi-Tropical (Zone 1 Ferguson)
 *
 * NOTE: Les données sont définies dans climate_presets.c
 */

#ifndef CLIMATE_PRESETS_H
#define CLIMATE_PRESETS_H

#include "climate_types.h"
#include "lvgl.h" // Pour LV_SYMBOL_*

// ====================================================================================
// NOMS DES TYPES DE TERRARIUMS (extern - définis dans climate_presets.c)
// ====================================================================================

extern const char *terrarium_type_names[];
extern const char *terrarium_type_icons[];
extern const char *terrarium_type_examples[];

// ====================================================================================
// ZONES DE FERGUSON
// ====================================================================================

typedef struct {
  float uvi_min;
  float uvi_max;
  const char *description;
} ferguson_zone_info_t;

extern const char *ferguson_zone_names[];
extern const ferguson_zone_info_t ferguson_zones[];

// ====================================================================================
// PRESETS DE TERRARIUMS (extern - définis dans climate_presets.c)
// ====================================================================================

extern const terrarium_config_t PRESET_DESERT;
extern const terrarium_config_t PRESET_SEMI_DESERT;
extern const terrarium_config_t PRESET_TROPICAL;
extern const terrarium_config_t PRESET_SEMI_TROPICAL;

extern const terrarium_config_t *TERRARIUM_PRESETS[];

// ====================================================================================
// FONCTIONS UTILITAIRES (inline pour éviter les problèmes de linkage)
// ====================================================================================

/**
 * @brief Obtenir le preset pour un type de terrarium
 * @param type Type de terrarium
 * @return Pointeur vers la configuration preset (const)
 */
static inline const terrarium_config_t *
climate_get_preset(terrarium_type_t type) {
  if (type >= TERRARIUM_TYPE_COUNT) {
    return &PRESET_DESERT; // Défaut
  }
  return TERRARIUM_PRESETS[type];
}

/**
 * @brief Obtenir le nom d'un type de terrarium
 */
static inline const char *climate_get_type_name(terrarium_type_t type) {
  if (type >= TERRARIUM_TYPE_COUNT) {
    return "Inconnu";
  }
  return terrarium_type_names[type];
}

/**
 * @brief Obtenir l'icône d'un type de terrarium
 */
static inline const char *climate_get_type_icon(terrarium_type_t type) {
  if (type >= TERRARIUM_TYPE_COUNT) {
    return "❓";
  }
  return terrarium_type_icons[type];
}

/**
 * @brief Obtenir les exemples d'espèces pour un type
 */
static inline const char *climate_get_type_examples(terrarium_type_t type) {
  if (type >= TERRARIUM_TYPE_COUNT) {
    return "";
  }
  return terrarium_type_examples[type];
}

/**
 * @brief Obtenir les infos d'une zone Ferguson
 */
static inline const ferguson_zone_info_t *
climate_get_ferguson_info(ferguson_zone_t zone) {
  if (zone < 1 || zone > 4) {
    return &ferguson_zones[1]; // Défaut Zone 1
  }
  return &ferguson_zones[zone];
}

#endif // CLIMATE_PRESETS_H
