/**
 * @file climate_types.h
 * @brief Data types for Climate Control System
 * @version 1.0
 * @date 2026-01-06
 *
 * Types de terrariums, capteurs, actionneurs et configurations
 * pour la simulation de gestion climatique multi-terrariums.
 */

#ifndef CLIMATE_TYPES_H
#define CLIMATE_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>


// ====================================================================================
// ENUMERATIONS
// ====================================================================================

/**
 * @brief Types de terrariums
 */
typedef enum {
  TERRARIUM_DESERT = 0,  // 🏜️ Désertique (Pogona, Uromastyx, Varanidae)
  TERRARIUM_SEMI_DESERT, // 🌵 Semi-Désertique (Python royal, Serpent des blés)
  TERRARIUM_TROPICAL,    // 🌴 Tropical (Python vert, Dendrobates, Caméléon)
  TERRARIUM_SEMI_TROPICAL, // 🌿 Semi-Tropical (Boa, Morelia, Gecko à crête)
  TERRARIUM_TYPE_COUNT
} terrarium_type_t;

/**
 * @brief Zones de Ferguson (UV Index)
 * Classification UV pour reptiles selon Dr. Gary Ferguson
 */
typedef enum {
  FERGUSON_ZONE_1 =
      1, // UVI 0.0-0.7 - Crépusculaire/Ombre (Geckos nocturnes, serpents)
  FERGUSON_ZONE_2 = 2, // UVI 0.7-1.0 - Ombre partielle (Boa, Python tropical)
  FERGUSON_ZONE_3 = 3, // UVI 1.0-2.6 - Soleil filtré (Caméléon, some varanidae)
  FERGUSON_ZONE_4 =
      4 // UVI 2.6-3.5 - Plein soleil (Pogona, Uromastyx, desert varanidae)
} ferguson_zone_t;

/**
 * @brief État des équipements
 */
typedef enum {
  EQUIPMENT_OFF = 0,
  EQUIPMENT_ON,
  EQUIPMENT_AUTO,
  EQUIPMENT_ERROR
} equipment_state_t;

/**
 * @brief Types d'alertes
 */
typedef enum {
  ALERT_NONE = 0,
  ALERT_TEMP_HIGH,           // Température trop haute
  ALERT_TEMP_LOW,            // Température trop basse
  ALERT_HUMIDITY_HIGH,       // Humidité trop haute
  ALERT_HUMIDITY_LOW,        // Humidité trop basse
  ALERT_WATER_BASIN_LOW,     // Niveau bassin bas
  ALERT_WATER_RESERVOIR_LOW, // Niveau réservoir bas
  ALERT_EQUIPMENT_FAILURE,   // Panne équipement
  ALERT_UV_INDEX_HIGH,       // UV trop fort
  ALERT_UV_INDEX_LOW         // UV trop faible
} alert_type_t;

/**
 * @brief Priorité des alertes
 */
typedef enum {
  ALERT_PRIORITY_INFO = 0,
  ALERT_PRIORITY_WARNING,
  ALERT_PRIORITY_CRITICAL
} alert_priority_t;

/**
 * @brief Mode de communication
 */
typedef enum {
  COMM_MODE_WIFI = 0,
  COMM_MODE_BLE,
  COMM_MODE_ESPNOW,
  COMM_MODE_NONE
} comm_mode_t;

// ====================================================================================
// STRUCTURES - CAPTEURS
// ====================================================================================

/**
 * @brief Données des capteurs d'un terrarium
 */
typedef struct {
  float temp_hot_zone;           // Température zone chaude (°C)
  float temp_cold_zone;          // Température zone froide (°C)
  float humidity;                // Humidité relative (%)
  float uv_index;                // Index UV mesuré
  uint8_t water_basin_level;     // Niveau bassin (0-100%)
  uint8_t water_reservoir_level; // Niveau réservoir brumisation (0-100%)
  time_t last_update;            // Timestamp dernière mise à jour
} sensor_data_t;

// ====================================================================================
// STRUCTURES - ÉQUIPEMENTS
// ====================================================================================

/**
 * @brief État des équipements d'un terrarium
 */
typedef struct {
  // Chauffage
  bool heating_on;
  uint8_t heating_power; // 0-100% (pour thermostat proportionnel)

  // Éclairage UV
  bool uv_lamp_on;
  uint8_t uv_intensity; // 0-100%

  // Éclairage jour/nuit
  bool day_light_on;

  // Brumisation
  bool misting_on;
  uint32_t misting_last_cycle; // Timestamp dernier cycle

  // Pompe cascade/bassin
  bool pump_on;

  // États d'erreur
  bool heating_error;
  bool uv_lamp_error;
  bool misting_error;
  bool pump_error;
} equipment_state_data_t;

// ====================================================================================
// STRUCTURES - PROGRAMMATION
// ====================================================================================

/**
 * @brief Programmation horaire pour un équipement
 */
typedef struct {
  uint8_t on_hour;    // Heure d'allumage (0-23)
  uint8_t on_minute;  // Minute d'allumage (0-59)
  uint8_t off_hour;   // Heure d'extinction (0-23)
  uint8_t off_minute; // Minute d'extinction (0-59)
  bool enabled;       // Programmation active
  bool days[7];       // Jours actifs (0=Lun, 6=Dim)
} schedule_t;

/**
 * @brief Programmation de la brumisation
 */
typedef struct {
  uint16_t
      interval_minutes; // Intervalle entre cycles (minutes) - uint16 pour > 4h
  uint8_t duration_seconds; // Durée de chaque cycle (secondes)
  uint8_t start_hour;       // Heure de début (0-23)
  uint8_t end_hour;         // Heure de fin (0-23)
  bool enabled;
} misting_schedule_t;

// ====================================================================================
// STRUCTURES - CONFIGURATION TERRARIUM
// ====================================================================================

/**
 * @brief Configuration complète d'un terrarium
 */
typedef struct {
  // Identification
  uint8_t id;
  char name[32];
  terrarium_type_t type;
  ferguson_zone_t uv_zone;
  bool active; // Terrarium actif dans la simulation

  // Consignes de température (°C)
  float temp_day_hot_min;  // Zone chaude jour - minimum
  float temp_day_hot_max;  // Zone chaude jour - maximum
  float temp_day_cold_min; // Zone froide jour - minimum
  float temp_day_cold_max; // Zone froide jour - maximum
  float temp_night_min;    // Nuit - minimum (toutes zones)
  float temp_night_max;    // Nuit - maximum (toutes zones)

  // Consignes d'humidité (%)
  uint8_t humidity_min;
  uint8_t humidity_max;

  // Seuils d'alerte température (écart par rapport aux consignes)
  float temp_alert_threshold; // Degrés d'écart pour déclencher alerte

  // Seuils d'alerte eau
  uint8_t water_basin_alert;     // % minimum bassin
  uint8_t water_reservoir_alert; // % minimum réservoir

  // Programmations
  schedule_t light_schedule;   // Éclairage jour
  schedule_t uv_schedule;      // Lampe UV
  schedule_t heating_schedule; // Chauffage (si pas thermostat auto)
  schedule_t pump_schedule;    // Pompe cascade
  misting_schedule_t misting;  // Brumisation

  // Données en temps réel
  sensor_data_t sensors;
  equipment_state_data_t equipment;

} terrarium_config_t;

// ====================================================================================
// STRUCTURES - ALERTES
// ====================================================================================

/**
 * @brief Alerte système
 */
typedef struct {
  uint8_t id;
  uint8_t terrarium_id;
  alert_type_t type;
  alert_priority_t priority;
  time_t timestamp;
  char message[64];
  bool acknowledged; // Alerte acquittée par l'utilisateur
  bool active;       // Alerte encore active
} alert_t;

// ====================================================================================
// STRUCTURES - HISTORIQUE
// ====================================================================================

/**
 * @brief Point de données pour l'historique
 */
typedef struct {
  time_t timestamp;
  float temp_hot;
  float temp_cold;
  float humidity;
  float uv_index;
} history_point_t;

// ====================================================================================
// STRUCTURES - COMMUNICATION
// ====================================================================================

/**
 * @brief Paquet de données pour communication inter-panels
 */
typedef struct {
  uint8_t terrarium_id;
  time_t timestamp;

  // Données climatiques
  float temp_hot;
  float temp_cold;
  float humidity;
  float uv_index;

  // États équipements (bitfield)
  uint8_t equipment_states; // Bits: 0=heating, 1=uv, 2=light, 3=misting, 4=pump

  // Alertes actives (bitfield)
  uint16_t active_alerts; // Correspond aux alert_type_t

} climate_packet_t;

// ====================================================================================
// CONSTANTES
// ====================================================================================

#define MAX_TERRARIUMS 8
#define MAX_ALERTS 32
#define MAX_HISTORY_POINTS 288 // 24h avec 1 point toutes les 5 minutes

// Limites physiques
#define TEMP_MIN_VALID 5.0f
#define TEMP_MAX_VALID 60.0f
#define HUMIDITY_MIN_VALID 0
#define HUMIDITY_MAX_VALID 100
#define UV_INDEX_MAX 10.0f

// Intervalles de mise à jour (ms)
#define SENSOR_UPDATE_INTERVAL_MS 1000  // 1 seconde
#define HISTORY_SAVE_INTERVAL_MS 300000 // 5 minutes
#define ALERT_CHECK_INTERVAL_MS 5000    // 5 secondes
#define COMM_SEND_INTERVAL_MS 10000     // 10 secondes

#endif // CLIMATE_TYPES_H
