/**
 * @file pet_simulator.h
 * @brief Moteur de simulation pour l'élevage virtuel de Tribolonotus
 */

#ifndef PET_SIMULATOR_H
#define PET_SIMULATOR_H

#include "tribolonotus_types.h"
#include <stdbool.h>
#include <stdint.h>

// ====================================================================================
// INITIALISATION ET GESTION
// ====================================================================================

/**
 * @brief Initialise le simulateur (charge depuis NVS si sauvegarde existe)
 */
void pet_simulator_init(void);

/**
 * @brief Mise à jour globale du simulateur (appelé chaque seconde)
 */
void pet_simulator_update(void);

/**
 * @brief Sauvegarde l'état complet dans NVS
 */
void pet_simulator_save(void);

/**
 * @brief Charge l'état depuis NVS
 * @return true si succès, false sinon
 */
bool pet_simulator_load(void);

/**
 * @brief Réinitialise complètement le jeu (nouvelle partie)
 */
void pet_simulator_reset(void);

// ====================================================================================
// GESTION DES LÉZARDS
// ====================================================================================

/**
 * @brief Crée un nouveau lézard
 * @param species Espèce du lézard
 * @param name Nom personnalisé
 * @param sex Sexe (ou SEX_UNKNOWN)
 * @return Index du lézard créé, ou -1 si échec
 */
int8_t pet_create(tribolonotus_species_t species, const char *name, sex_t sex);

/**
 * @brief Supprime un lézard (décès ou libération)
 * @param pet_index Index du lézard
 */
void pet_remove(uint8_t pet_index);

/**
 * @brief Obtient un pointeur vers un lézard
 * @param pet_index Index du lézard
 * @return Pointeur vers pet_t ou NULL
 */
pet_t *pet_get(uint8_t pet_index);

/**
 * @brief Obtient le lézard actuellement sélectionné
 * @return Pointeur vers pet_t ou NULL
 */
pet_t *pet_get_current(void);

/**
 * @brief Change le lézard sélectionné
 * @param pet_index Nouvel index
 */
void pet_set_current(uint8_t pet_index);

/**
 * @brief Obtient l'index du lézard actuellement sélectionné
 * @return Index du lézard courant
 */
uint8_t pet_get_current_index(void);

/**
 * @brief Obtient le nombre de lézards vivants
 * @return Nombre de lézards
 */
uint8_t pet_get_count(void);

// ====================================================================================
// ACTIONS DU JOUEUR
// ====================================================================================

/**
 * @brief Nourrit le lézard
 * @param pet_index Index du lézard
 * @param food_type Type de nourriture
 * @return true si succès
 */
bool pet_feed(uint8_t pet_index, food_type_t food_type);

/**
 * @brief Donne à boire au lézard
 * @param pet_index Index du lézard
 * @return true si succès
 */
bool pet_water(uint8_t pet_index);

/**
 * @brief Active le chauffage (zone chaude)
 * @param pet_index Index du lézard
 * @param duration_minutes Durée en minutes
 * @return true si succès
 */
bool pet_heat(uint8_t pet_index, uint8_t duration_minutes);

/**
 * @brief Brumise le terrarium (augmente humidité)
 * @param pet_index Index du lézard
 * @return true si succès
 */
bool pet_mist(uint8_t pet_index);

/**
 * @brief Nettoie le terrarium
 * @param pet_index Index du lézard
 * @return true si succès
 */
bool pet_clean(uint8_t pet_index);

/**
 * @brief Interagit avec le lézard (jouer, câlins)
 * @param pet_index Index du lézard
 * @return true si succès
 */
bool pet_play(uint8_t pet_index);

/**
 * @brief Soigne le lézard (vétérinaire)
 * @param pet_index Index du lézard
 * @return true si succès
 */
bool pet_heal(uint8_t pet_index);

/**
 * @brief Tente de reproduire deux lézards
 * @param female_index Index de la femelle
 * @param male_index Index du mâle
 * @return true si reproduction réussie
 */
bool pet_breed(uint8_t female_index, uint8_t male_index);

// ====================================================================================
// SYSTÈME DE CROISSANCE
// ====================================================================================

/**
 * @brief Met à jour l'âge et le stade de croissance
 * @param pet Pointeur vers le lézard
 */
void pet_update_growth(pet_t *pet);

/**
 * @brief Met à jour le poids et la taille en fonction de l'âge
 * @param pet Pointeur vers le lézard
 */
void pet_update_size(pet_t *pet);

/**
 * @brief Détermine le sexe (si encore inconnu)
 * @param pet Pointeur vers le lézard
 */
void pet_determine_sex(pet_t *pet);

// ====================================================================================
// SYSTÈME DE BESOINS
// ====================================================================================

/**
 * @brief Met à jour tous les besoins vitaux (décroissance naturelle)
 * @param pet Pointeur vers le lézard
 * @param elapsed_seconds Temps écoulé depuis dernière MAJ
 */
void pet_update_needs(pet_t *pet, uint32_t elapsed_seconds);

/**
 * @brief Calcule l'humeur en fonction des besoins
 * @param pet Pointeur vers le lézard
 * @return Nouvelle humeur
 */
mood_t pet_calculate_mood(const pet_t *pet);

/**
 * @brief Calcule l'état de santé en fonction des besoins
 * @param pet Pointeur vers le lézard
 * @return Nouvel état de santé
 */
health_status_t pet_calculate_health(const pet_t *pet);

// ====================================================================================
// SYSTÈME DE SANTÉ
// ====================================================================================

/**
 * @brief Met à jour la santé du lézard
 * @param pet Pointeur vers le lézard
 */
void pet_update_health(pet_t *pet);

/**
 * @brief Vérifie si le lézard peut mourir
 * @param pet Pointeur vers le lézard
 * @return true si mort survenue
 */
bool pet_check_death(pet_t *pet);

/**
 * @brief Déclenche une mue aléatoire
 * @param pet Pointeur vers le lézard
 */
void pet_trigger_shedding(pet_t *pet);

// ====================================================================================
// INVENTAIRE ET ARGENT
// ====================================================================================

/**
 * @brief Obtient l'inventaire du joueur
 * @return Pointeur vers l'inventaire
 */
inventory_t *pet_get_inventory(void);

/**
 * @brief Achète de la nourriture
 * @param food_type Type de nourriture
 * @param quantity Quantité
 * @return true si achat réussi
 */
bool shop_buy_food(food_type_t food_type, uint16_t quantity);

/**
 * @brief Obtient l'argent du joueur
 * @return Montant en $
 */
uint32_t pet_get_money(void);

/**
 * @brief Ajoute de l'argent
 * @param amount Montant à ajouter
 */
void pet_add_money(uint32_t amount);

/**
 * @brief Retire de l'argent
 * @param amount Montant à retirer
 * @return true si succès
 */
bool pet_remove_money(uint32_t amount);

// ====================================================================================
// UTILITAIRES
// ====================================================================================

/**
 * @brief Obtient les informations d'une espèce
 * @param species Espèce
 * @return Pointeur vers les données constantes
 */
const species_info_t *pet_get_species_info(tribolonotus_species_t species);

/**
 * @brief Convertit un stade de croissance en chaîne
 * @param stage Stade
 * @return Chaîne de caractères
 */
const char *pet_stage_to_string(growth_stage_t stage);

/**
 * @brief Convertit un état de santé en chaîne
 * @param health État de santé
 * @return Chaîne de caractères
 */
const char *pet_health_to_string(health_status_t health);

/**
 * @brief Convertit une humeur en chaîne
 * @param mood Humeur
 * @return Chaîne de caractères
 */
const char *pet_mood_to_string(mood_t mood);

/**
 * @brief Obtient le temps de jeu total
 * @return Temps en secondes
 */
uint32_t pet_get_playtime(void);

#endif // PET_SIMULATOR_H
