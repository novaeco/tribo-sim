/**
 * @file climate_presets.c
 * @brief Implementation des presets de configuration pour terrariums
 * @version 1.0
 * @date 2026-01-08
 *
 * Ce fichier contient les données statiques des presets.
 * Les déclarations sont dans climate_presets.h
 */

#include "climate_presets.h"

// ====================================================================================
// NOMS DES TYPES DE TERRARIUMS
// ====================================================================================

const char *terrarium_type_names[] = {
    "Désertique",      // TERRARIUM_DESERT
    "Semi-Désertique", // TERRARIUM_SEMI_DESERT
    "Tropical",        // TERRARIUM_TROPICAL
    "Semi-Tropical"    // TERRARIUM_SEMI_TROPICAL
};

const char *terrarium_type_icons[] = {
    LV_SYMBOL_CHARGE,   // TERRARIUM_DESERT (soleil/chaleur)
    LV_SYMBOL_EYE_OPEN, // TERRARIUM_SEMI_DESERT (surveillance)
    LV_SYMBOL_REFRESH,  // TERRARIUM_TROPICAL (humidité/cycle)
    LV_SYMBOL_LOOP      // TERRARIUM_SEMI_TROPICAL (équilibre)
};

const char *terrarium_type_examples[] = {
    "Pogona, Uromastyx, Varanidae", "Python royal, Serpent des blés, Varanidae",
    "Python vert, Dendrobates, Caméléon", "Boa, Morelia, Gecko à crête"};

// ====================================================================================
// ZONES DE FERGUSON
// ====================================================================================

const char *ferguson_zone_names[] = {
    "",                         // Index 0 non utilisé
    "Zone 1 - Crépusculaire",   // UVI 0.0-0.7
    "Zone 2 - Ombre partielle", // UVI 0.7-1.0
    "Zone 3 - Soleil filtré",   // UVI 1.0-2.6
    "Zone 4 - Plein soleil"     // UVI 2.6-3.5
};

const ferguson_zone_info_t ferguson_zones[] = {
    {0.0f, 0.0f, ""},                                        // Index 0
    {0.0f, 0.7f, "Geckos nocturnes, serpents nocturnes"},    // Zone 1
    {0.7f, 1.0f, "Boa, Pythons tropicaux, Gecko à crête"},   // Zone 2
    {1.0f, 2.6f, "Caméléon, Basilic, certains Varanidae"},   // Zone 3
    {2.6f, 3.5f, "Pogona, Uromastyx, Varanidae désertiques"} // Zone 4
};

// ====================================================================================
// PRESETS DE TERRARIUMS
// ====================================================================================

const terrarium_config_t PRESET_DESERT = {
    .id = 0,
    .name = "Terrarium Désertique",
    .type = TERRARIUM_DESERT,
    .uv_zone = FERGUSON_ZONE_4,
    .active = true,

    // Températures (°C)
    .temp_day_hot_min = 38.0f,
    .temp_day_hot_max = 45.0f, // Point chaud jusqu'à 45°C
    .temp_day_cold_min = 28.0f,
    .temp_day_cold_max = 32.0f,
    .temp_night_min = 20.0f,
    .temp_night_max = 25.0f,

    // Humidité (%)
    .humidity_min = 20,
    .humidity_max = 35,

    // Seuils d'alerte
    .temp_alert_threshold = 5.0f, // Alerte si +/- 5°C
    .water_basin_alert = 20,
    .water_reservoir_alert = 15,

    // Éclairage jour: 6h00 - 20h00 (14h)
    .light_schedule = {.on_hour = 6,
                       .on_minute = 0,
                       .off_hour = 20,
                       .off_minute = 0,
                       .enabled = true,
                       .days = {true, true, true, true, true, true, true}},

    // UV: 8h00 - 18h00 (10h)
    .uv_schedule = {.on_hour = 8,
                    .on_minute = 0,
                    .off_hour = 18,
                    .off_minute = 0,
                    .enabled = true,
                    .days = {true, true, true, true, true, true, true}},

    // Chauffage: 6h00 - 22h00
    .heating_schedule = {.on_hour = 6,
                         .on_minute = 0,
                         .off_hour = 22,
                         .off_minute = 0,
                         .enabled = true,
                         .days = {true, true, true, true, true, true, true}},

    // Pas de pompe par défaut
    .pump_schedule = {.enabled = false},

    // Brumisation minimale (1x/jour le matin)
    .misting = {
        .interval_minutes = 0, // Pas d'intervalle, juste matin
        .duration_seconds = 10,
        .start_hour = 7,
        .end_hour = 8,
        .enabled = false // Désactivé par défaut pour désertique
    }};

const terrarium_config_t PRESET_SEMI_DESERT = {
    .id = 0,
    .name = "Terrarium Semi-Désertique",
    .type = TERRARIUM_SEMI_DESERT,
    .uv_zone = FERGUSON_ZONE_3,
    .active = true,

    // Températures (°C)
    .temp_day_hot_min = 32.0f,
    .temp_day_hot_max = 38.0f, // Point chaud modéré
    .temp_day_cold_min = 25.0f,
    .temp_day_cold_max = 28.0f,
    .temp_night_min = 18.0f,
    .temp_night_max = 22.0f,

    // Humidité (%)
    .humidity_min = 30,
    .humidity_max = 50,

    // Seuils d'alerte
    .temp_alert_threshold = 4.0f,
    .water_basin_alert = 25,
    .water_reservoir_alert = 20,

    // Éclairage jour: 7h00 - 19h00 (12h)
    .light_schedule = {.on_hour = 7,
                       .on_minute = 0,
                       .off_hour = 19,
                       .off_minute = 0,
                       .enabled = true,
                       .days = {true, true, true, true, true, true, true}},

    // UV: 9h00 - 17h00 (8h)
    .uv_schedule = {.on_hour = 9,
                    .on_minute = 0,
                    .off_hour = 17,
                    .off_minute = 0,
                    .enabled = true,
                    .days = {true, true, true, true, true, true, true}},

    // Chauffage: 7h00 - 21h00
    .heating_schedule = {.on_hour = 7,
                         .on_minute = 0,
                         .off_hour = 21,
                         .off_minute = 0,
                         .enabled = true,
                         .days = {true, true, true, true, true, true, true}},

    // Pompe cascade optionnelle
    .pump_schedule = {.on_hour = 8,
                      .on_minute = 0,
                      .off_hour = 20,
                      .off_minute = 0,
                      .enabled = false},

    // Brumisation légère (2x/jour)
    .misting = {.interval_minutes = 360, // Toutes les 6h
                .duration_seconds = 15,
                .start_hour = 7,
                .end_hour = 19,
                .enabled = true}};

const terrarium_config_t PRESET_TROPICAL = {
    .id = 0,
    .name = "Terrarium Tropical",
    .type = TERRARIUM_TROPICAL,
    .uv_zone = FERGUSON_ZONE_2,
    .active = true,

    // Températures (°C)
    .temp_day_hot_min = 28.0f,
    .temp_day_hot_max = 32.0f,
    .temp_day_cold_min = 24.0f,
    .temp_day_cold_max = 27.0f,
    .temp_night_min = 22.0f,
    .temp_night_max = 26.0f,

    // Humidité (%) - Haute
    .humidity_min = 70,
    .humidity_max = 90,

    // Seuils d'alerte
    .temp_alert_threshold = 3.0f,
    .water_basin_alert = 30,
    .water_reservoir_alert = 25,

    // Éclairage jour: 7h00 - 19h00 (12h)
    .light_schedule = {.on_hour = 7,
                       .on_minute = 0,
                       .off_hour = 19,
                       .off_minute = 0,
                       .enabled = true,
                       .days = {true, true, true, true, true, true, true}},

    // UV: 9h00 - 16h00 (7h) - Plus court pour tropical
    .uv_schedule = {.on_hour = 9,
                    .on_minute = 0,
                    .off_hour = 16,
                    .off_minute = 0,
                    .enabled = true,
                    .days = {true, true, true, true, true, true, true}},

    // Chauffage: Toute la journée (thermostat)
    .heating_schedule = {.on_hour = 0,
                         .on_minute = 0,
                         .off_hour = 23,
                         .off_minute = 59,
                         .enabled = true,
                         .days = {true, true, true, true, true, true, true}},

    // Pompe cascade active
    .pump_schedule = {.on_hour = 7,
                      .on_minute = 0,
                      .off_hour = 21,
                      .off_minute = 0,
                      .enabled = true,
                      .days = {true, true, true, true, true, true, true}},

    // Brumisation fréquente (4-5x/jour)
    .misting = {.interval_minutes = 120, // Toutes les 2h
                .duration_seconds = 30,
                .start_hour = 7,
                .end_hour = 21,
                .enabled = true}};

const terrarium_config_t PRESET_SEMI_TROPICAL = {
    .id = 0,
    .name = "Terrarium Semi-Tropical",
    .type = TERRARIUM_SEMI_TROPICAL,
    .uv_zone = FERGUSON_ZONE_1,
    .active = true,

    // Températures (°C)
    .temp_day_hot_min = 28.0f,
    .temp_day_hot_max = 32.0f,
    .temp_day_cold_min = 24.0f,
    .temp_day_cold_max = 26.0f,
    .temp_night_min = 20.0f,
    .temp_night_max = 24.0f,

    // Humidité (%)
    .humidity_min = 50,
    .humidity_max = 70,

    // Seuils d'alerte
    .temp_alert_threshold = 4.0f,
    .water_basin_alert = 25,
    .water_reservoir_alert = 20,

    // Éclairage jour: 7h00 - 19h00 (12h)
    .light_schedule = {.on_hour = 7,
                       .on_minute = 0,
                       .off_hour = 19,
                       .off_minute = 0,
                       .enabled = true,
                       .days = {true, true, true, true, true, true, true}},

    // UV: Léger, 10h00 - 15h00 (5h)
    .uv_schedule = {.on_hour = 10,
                    .on_minute = 0,
                    .off_hour = 15,
                    .off_minute = 0,
                    .enabled = true,
                    .days = {true, true, true, true, true, true, true}},

    // Chauffage
    .heating_schedule = {.on_hour = 6,
                         .on_minute = 0,
                         .off_hour = 22,
                         .off_minute = 0,
                         .enabled = true,
                         .days = {true, true, true, true, true, true, true}},

    // Pompe cascade
    .pump_schedule = {.on_hour = 8,
                      .on_minute = 0,
                      .off_hour = 20,
                      .off_minute = 0,
                      .enabled = true,
                      .days = {true, true, true, true, true, true, true}},

    // Brumisation modérée (2-3x/jour)
    .misting = {.interval_minutes = 240, // Toutes les 4h
                .duration_seconds = 20,
                .start_hour = 8,
                .end_hour = 20,
                .enabled = true}};

// ====================================================================================
// TABLEAU DES PRESETS
// ====================================================================================

const terrarium_config_t *TERRARIUM_PRESETS[] = {
    &PRESET_DESERT, &PRESET_SEMI_DESERT, &PRESET_TROPICAL,
    &PRESET_SEMI_TROPICAL};
