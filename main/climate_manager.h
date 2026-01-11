/**
 * @file climate_manager.h
 * @brief Climate Control Manager - Simulation engine
 * @version 1.0
 * @date 2026-01-06
 *
 * Moteur de simulation pour la gestion climatique multi-terrariums.
 * Gère les capteurs simulés, les équipements et les alertes.
 */

#ifndef CLIMATE_MANAGER_H
#define CLIMATE_MANAGER_H

#include "climate_presets.h"
#include "climate_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================================
// INITIALISATION
// ====================================================================================

/**
 * @brief Initialiser le gestionnaire climatique
 * @return ESP_OK si succès
 */
esp_err_t climate_manager_init(void);

/**
 * @brief Démarrer la tâche de simulation
 * @return ESP_OK si succès
 */
esp_err_t climate_manager_start(void);

/**
 * @brief Arrêter la tâche de simulation
 */
void climate_manager_stop(void);

// ====================================================================================
// GESTION DES TERRARIUMS
// ====================================================================================

/**
 * @brief Ajouter un nouveau terrarium
 * @param type Type de terrarium (preset utilisé)
 * @param name Nom personnalisé (peut être NULL pour nom par défaut)
 * @return ID du terrarium créé, ou -1 si erreur
 */
int climate_add_terrarium(terrarium_type_t type, const char *name);

/**
 * @brief Supprimer un terrarium
 * @param id ID du terrarium
 * @return ESP_OK si succès
 */
esp_err_t climate_remove_terrarium(uint8_t id);

/**
 * @brief Obtenir la configuration d'un terrarium
 * @param id ID du terrarium
 * @return Pointeur vers la config (NULL si non trouvé)
 */
terrarium_config_t *climate_get_terrarium(uint8_t id);

/**
 * @brief Obtenir le nombre de terrariums actifs
 */
uint8_t climate_get_terrarium_count(void);

/**
 * @brief Obtenir tous les terrariums
 * @param[out] terrariums Tableau de pointeurs (doit avoir MAX_TERRARIUMS
 * éléments)
 * @return Nombre de terrariums copiés
 */
uint8_t climate_get_all_terrariums(terrarium_config_t **terrariums);

// ====================================================================================
// LECTURE DES CAPTEURS (SIMULATION)
// ====================================================================================

/**
 * @brief Obtenir les données capteurs d'un terrarium
 * @param id ID du terrarium
 * @return Pointeur vers les données (NULL si non trouvé)
 */
const sensor_data_t *climate_get_sensors(uint8_t id);

/**
 * @brief Forcer une mise à jour des capteurs (utile pour debug)
 * @param id ID du terrarium
 */
void climate_update_sensors(uint8_t id);

// ====================================================================================
// CONTRÔLE DES ÉQUIPEMENTS
// ====================================================================================

/**
 * @brief Définir l'état du chauffage
 * @param id ID du terrarium
 * @param on État ON/OFF
 * @param power Puissance 0-100% (si applicable)
 */
esp_err_t climate_set_heating(uint8_t id, bool on, uint8_t power);

/**
 * @brief Définir l'état de la lampe UV
 * @param id ID du terrarium
 * @param on État ON/OFF
 * @param intensity Intensité 0-100%
 */
esp_err_t climate_set_uv_lamp(uint8_t id, bool on, uint8_t intensity);

/**
 * @brief Définir l'état de l'éclairage jour
 * @param id ID du terrarium
 * @param on État ON/OFF
 */
esp_err_t climate_set_day_light(uint8_t id, bool on);

/**
 * @brief Déclencher un cycle de brumisation
 * @param id ID du terrarium
 */
esp_err_t climate_trigger_misting(uint8_t id);

/**
 * @brief Définir l'état de la pompe
 * @param id ID du terrarium
 * @param on État ON/OFF
 */
esp_err_t climate_set_pump(uint8_t id, bool on);

/**
 * @brief Obtenir l'état des équipements d'un terrarium
 * @param id ID du terrarium
 * @return Pointeur vers l'état (NULL si non trouvé)
 */
const equipment_state_data_t *climate_get_equipment_state(uint8_t id);

// ====================================================================================
// PROGRAMMATION
// ====================================================================================

/**
 * @brief Définir la programmation de l'éclairage
 * @param id ID du terrarium
 * @param schedule Nouvelle programmation
 */
esp_err_t climate_set_light_schedule(uint8_t id, const schedule_t *schedule);

/**
 * @brief Définir la programmation UV
 * @param id ID du terrarium
 * @param schedule Nouvelle programmation
 */
esp_err_t climate_set_uv_schedule(uint8_t id, const schedule_t *schedule);

/**
 * @brief Définir la programmation de brumisation
 * @param id ID du terrarium
 * @param schedule Nouvelle programmation
 */
esp_err_t climate_set_misting_schedule(uint8_t id,
                                       const misting_schedule_t *schedule);

/**
 * @brief Définir la programmation de la pompe
 * @param id ID du terrarium
 * @param schedule Nouvelle programmation
 */
esp_err_t climate_set_pump_schedule(uint8_t id, const schedule_t *schedule);

// ====================================================================================
// ALERTES
// ====================================================================================

/**
 * @brief Obtenir le nombre d'alertes actives
 */
uint8_t climate_get_active_alert_count(void);

/**
 * @brief Obtenir toutes les alertes actives
 * @param[out] alerts Tableau d'alertes (doit avoir MAX_ALERTS éléments)
 * @return Nombre d'alertes copiées
 */
uint8_t climate_get_active_alerts(alert_t *alerts);

/**
 * @brief Acquitter une alerte
 * @param alert_id ID de l'alerte
 */
esp_err_t climate_acknowledge_alert(uint8_t alert_id);

/**
 * @brief Acquitter toutes les alertes d'un terrarium
 * @param terrarium_id ID du terrarium
 */
esp_err_t climate_acknowledge_all_alerts(uint8_t terrarium_id);

/**
 * @brief Effacer les alertes acquittées
 */
void climate_clear_acknowledged_alerts(void);

// ====================================================================================
// HISTORIQUE
// ====================================================================================

/**
 * @brief Obtenir l'historique d'un terrarium
 * @param id ID du terrarium
 * @param[out] points Tableau de points (doit avoir MAX_HISTORY_POINTS éléments)
 * @param max_points Nombre maximum de points à récupérer
 * @return Nombre de points copiés
 */
uint16_t climate_get_history(uint8_t id, history_point_t *points,
                             uint16_t max_points);

// ====================================================================================
// SIMULATION - Contrôles avancés
// ====================================================================================

/**
 * @brief Définir le multiplicateur de temps pour la simulation
 * @param multiplier Multiplicateur (1.0 = temps réel, 10.0 = 10x plus rapide)
 */
void climate_set_time_multiplier(float multiplier);

/**
 * @brief Simuler un remplissage d'eau
 * @param id ID du terrarium
 * @param basin_level Nouveau niveau bassin (0-100)
 * @param reservoir_level Nouveau niveau réservoir (0-100)
 */
esp_err_t climate_refill_water(uint8_t id, uint8_t basin_level,
                               uint8_t reservoir_level);

/**
 * @brief Simuler une panne d'équipement (pour test)
 * @param id ID du terrarium
 * @param equipment "heating", "uv", "misting", "pump"
 * @param failed true = en panne, false = réparé
 */
esp_err_t climate_simulate_equipment_failure(uint8_t id, const char *equipment,
                                             bool failed);

// ====================================================================================
// COMMUNICATION
// ====================================================================================

/**
 * @brief Définir le mode de communication
 * @param mode Mode (WIFI, BLE, ESPNOW, NONE)
 */
void climate_set_comm_mode(comm_mode_t mode);

/**
 * @brief Obtenir le mode de communication actuel
 */
comm_mode_t climate_get_comm_mode(void);

/**
 * @brief Préparer un paquet de données pour envoi
 * @param id ID du terrarium
 * @param[out] packet Paquet à remplir
 * @return ESP_OK si succès
 */
esp_err_t climate_prepare_packet(uint8_t id, climate_packet_t *packet);

// ====================================================================================
// SAUVEGARDE / CHARGEMENT
// ====================================================================================

/**
 * @brief Sauvegarder la configuration dans NVS
 * @return ESP_OK si succès
 */
esp_err_t climate_save_config(void);

/**
 * @brief Charger la configuration depuis NVS
 * @return ESP_OK si succès
 */
esp_err_t climate_load_config(void);

/**
 * @brief Exporter l'historique vers SD card (CSV)
 * @param id ID du terrarium
 * @param filepath Chemin du fichier
 * @return ESP_OK si succès
 */
esp_err_t climate_export_history_csv(uint8_t id, const char *filepath);

#ifdef __cplusplus
}
#endif

#endif // CLIMATE_MANAGER_H
