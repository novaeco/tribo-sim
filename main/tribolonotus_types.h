/**
 * @file tribolonotus_types.h
 * @brief Types et structures pour le simulateur d'élevage de Tribolonotus
 *
 * Simulation virtuelle type Tamagotchi avec 10 espèces de lézards crocodiles
 */

#ifndef TRIBOLONOTUS_TYPES_H
#define TRIBOLONOTUS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// ====================================================================================
// CONSTANTES
// ====================================================================================

#define MAX_PETS 6                    // Maximum 6 lézards simultanés
#define PET_NAME_MAX_LEN 16           // Longueur max du nom
#define MAX_AGE_DAYS 3650             // 10 ans max (en jours)
#define TICKS_PER_SECOND 1            // Fréquence de mise à jour (1 Hz)

// Seuils critiques (0-100)
#define CRITICAL_LEVEL 20
#define LOW_LEVEL 40
#define GOOD_LEVEL 60

// Vitesses de décroissance (par minute)
#define HUNGER_DECAY_RATE 2           // Faim augmente vite
#define THIRST_DECAY_RATE 3           // Soif augmente très vite
#define TEMPERATURE_DECAY_RATE 1      // Température baisse lentement
#define HUMIDITY_DECAY_RATE 2         // Humidité baisse
#define CLEANLINESS_DECAY_RATE 1      // Propreté baisse lentement
#define HAPPINESS_DECAY_RATE 1        // Bonheur baisse lentement

// ====================================================================================
// ÉNUMÉRATIONS
// ====================================================================================

/**
 * @brief Les 10 espèces de Tribolonotus
 */
typedef enum {
    SPECIES_T_GRACILIS = 0,           // Red-eyed crocodile skink (le plus populaire)
    SPECIES_T_NOVAEGUINEAE,           // Nouvelle-Guinée
    SPECIES_T_PONCELETI,              // Ponceleti
    SPECIES_T_PSYCHOSAUROPUS,         // Psychosauropus
    SPECIES_T_PSEUDOPONCELETI,        // Pseudo-ponceleti
    SPECIES_T_BRONGERSMAI,            // Brongersma's
    SPECIES_T_ANNECTENS,              // Annectens
    SPECIES_T_PARKERI,                // Parker's
    SPECIES_T_BLANCHARDI,             // Blanchard's
    SPECIES_T_SCHMIDTI,               // Schmidt's
    SPECIES_COUNT
} tribolonotus_species_t;

/**
 * @brief Stades de croissance
 */
typedef enum {
    STAGE_EGG = 0,                    // Œuf (0-60 jours)
    STAGE_HATCHLING,                  // Nouveau-né (60-180 jours)
    STAGE_JUVENILE,                   // Juvénile (180-365 jours)
    STAGE_SUBADULT,                   // Sub-adulte (1-2 ans)
    STAGE_ADULT,                      // Adulte (2+ ans)
    STAGE_COUNT
} growth_stage_t;

/**
 * @brief Sexe du lézard
 */
typedef enum {
    SEX_UNKNOWN = 0,                  // Sexe non déterminé (jeunes)
    SEX_MALE,                         // Mâle
    SEX_FEMALE,                       // Femelle
    SEX_COUNT
} sex_t;

/**
 * @brief États de santé
 */
typedef enum {
    HEALTH_DEAD = 0,                  // Mort
    HEALTH_CRITICAL,                  // Critique (urgence vétérinaire)
    HEALTH_SICK,                      // Malade
    HEALTH_WEAK,                      // Faible
    HEALTH_GOOD,                      // Bonne santé
    HEALTH_EXCELLENT,                 // Excellente santé
    HEALTH_COUNT
} health_status_t;

/**
 * @brief Humeurs du lézard
 */
typedef enum {
    MOOD_DEPRESSED = 0,               // Déprimé
    MOOD_SAD,                         // Triste
    MOOD_NEUTRAL,                     // Neutre
    MOOD_CONTENT,                     // Content
    MOOD_HAPPY,                       // Heureux
    MOOD_ECSTATIC,                    // Extatique
    MOOD_COUNT
} mood_t;

/**
 * @brief Types d'interactions possibles
 */
typedef enum {
    ACTION_FEED = 0,                  // Nourrir (insectes)
    ACTION_WATER,                     // Donner à boire
    ACTION_HEAT,                      // Chauffer (zone chaude)
    ACTION_MIST,                      // Brumiser (humidité)
    ACTION_CLEAN,                     // Nettoyer terrarium
    ACTION_PLAY,                      // Jouer/interaction
    ACTION_VET,                       // Soins vétérinaires
    ACTION_BREED,                     // Reproduction (si couple)
    ACTION_COUNT
} pet_action_t;

/**
 * @brief Types de nourriture
 */
typedef enum {
    FOOD_CRICKET = 0,                 // Grillon
    FOOD_DUBIA,                       // Blatte Dubia
    FOOD_WAXWORM,                     // Ver de farine
    FOOD_ISOPOD,                      // Cloporte
    FOOD_EARTHWORM,                   // Ver de terre
    FOOD_COUNT
} food_type_t;

// ====================================================================================
// STRUCTURES DE DONNÉES
// ====================================================================================

/**
 * @brief Informations sur une espèce
 */
typedef struct {
    tribolonotus_species_t id;
    const char *name_latin;           // Nom scientifique
    const char *name_common;          // Nom commun
    uint16_t adult_size_mm;           // Taille adulte (mm)
    uint16_t lifespan_years;          // Espérance de vie
    uint8_t rarity;                   // Rareté (1-10, 10=très rare)
    uint8_t difficulty;               // Difficulté d'élevage (1-10)

    // Besoins optimaux (température en °C)
    uint8_t temp_optimal_min;
    uint8_t temp_optimal_max;
    uint8_t humidity_optimal;         // Humidité optimale %

    // Reproduction
    uint16_t sexual_maturity_days;    // Maturité sexuelle (jours)
    uint8_t clutch_size;              // Taille ponte (œufs)
    uint16_t incubation_days;         // Durée incubation
} species_info_t;

/**
 * @brief Besoins et état physique du lézard (0-100 chacun)
 */
typedef struct {
    uint8_t hunger;                   // 100 = affamé, 0 = rassasié
    uint8_t thirst;                   // 100 = assoiffé, 0 = hydraté
    uint8_t temperature;              // 100 = optimal, 0 = hypothermie
    uint8_t humidity;                 // 100 = optimal, 0 = déshydratation
    uint8_t cleanliness;              // 100 = propre, 0 = sale
    uint8_t happiness;                // 100 = heureux, 0 = malheureux
    uint8_t energy;                   // 100 = éveillé, 0 = dort
} pet_needs_t;

/**
 * @brief Statistiques du lézard
 */
typedef struct {
    uint32_t age_days;                // Âge en jours
    uint16_t weight_grams;            // Poids en grammes
    uint16_t length_mm;               // Longueur en mm
    uint32_t total_feeds;             // Nombre de repas total
    uint32_t total_interactions;      // Nombre d'interactions
    uint32_t days_alive;              // Jours en vie
    uint16_t offspring_count;         // Nombre de descendants
} pet_stats_t;

/**
 * @brief État de santé détaillé
 */
typedef struct {
    health_status_t status;           // État général
    uint8_t health_points;            // Points de vie (0-100)
    bool is_shedding;                 // En mue ?
    bool is_pregnant;                 // Gravide ? (femelles)
    uint16_t days_until_eggs;         // Jours avant ponte
    uint32_t last_vet_visit;          // Timestamp dernière visite
    uint32_t last_illness;            // Timestamp dernière maladie
} pet_health_t;

/**
 * @brief Un lézard virtuel
 */
typedef struct {
    // Identité
    char name[PET_NAME_MAX_LEN];      // Nom donné par le joueur
    tribolonotus_species_t species;   // Espèce
    sex_t sex;                        // Sexe
    growth_stage_t stage;             // Stade de croissance
    uint32_t birth_timestamp;         // Timestamp naissance (epoch)
    uint32_t id;                      // ID unique

    // État physique
    pet_needs_t needs;                // Besoins vitaux
    pet_health_t health;              // Santé
    pet_stats_t stats;                // Statistiques
    mood_t mood;                      // Humeur actuelle

    // Génétique (pour reproduction)
    uint32_t parent1_id;              // ID parent 1
    uint32_t parent2_id;              // ID parent 2
    uint8_t color_variant;            // Variante de couleur (0-255)

    // Timestamps
    uint32_t last_fed;                // Dernier repas
    uint32_t last_watered;            // Dernière boisson
    uint32_t last_cleaned;            // Dernier nettoyage
    uint32_t last_interaction;        // Dernière interaction
    uint32_t last_update;             // Dernière mise à jour

    // État
    bool is_alive;                    // Vivant ?
    bool is_selected;                 // Sélectionné dans l'UI ?
} pet_t;

/**
 * @brief État global du simulateur
 */
typedef struct {
    pet_t pets[MAX_PETS];             // Liste des lézards
    uint8_t pet_count;                // Nombre de lézards actifs
    uint32_t player_money;            // Argent du joueur
    uint32_t game_start_time;         // Timestamp début de partie
    uint32_t total_playtime_seconds;  // Temps de jeu total
    uint8_t current_pet_index;        // Index lézard actuel
    uint32_t next_pet_id;             // Prochain ID à assigner
} game_state_t;

/**
 * @brief Inventaire du joueur
 */
typedef struct {
    uint16_t crickets;                // Grillons
    uint16_t dubias;                  // Blattes Dubia
    uint16_t waxworms;                // Vers de farine
    uint16_t isopods;                 // Cloportes
    uint16_t earthworms;              // Vers de terre
    uint8_t vitamin_powder;           // Poudre vitamines
    uint8_t calcium_powder;           // Poudre calcium
    uint8_t medications;              // Médicaments
} inventory_t;

// ====================================================================================
// DONNÉES CONSTANTES DES ESPÈCES
// ====================================================================================

static const species_info_t SPECIES_DATA[SPECIES_COUNT] = {
    // T. gracilis - Red-eyed crocodile skink (le plus commun)
    {
        .id = SPECIES_T_GRACILIS,
        .name_latin = "Tribolonotus gracilis",
        .name_common = "Scinque crocodile à œil rouge",
        .adult_size_mm = 80,
        .lifespan_years = 10,
        .rarity = 3,
        .difficulty = 6,
        .temp_optimal_min = 24,
        .temp_optimal_max = 28,
        .humidity_optimal = 80,
        .sexual_maturity_days = 730,  // ~2 ans
        .clutch_size = 1,              // Un seul œuf !
        .incubation_days = 60
    },

    // T. novaeguineae
    {
        .id = SPECIES_T_NOVAEGUINEAE,
        .name_latin = "Tribolonotus novaeguineae",
        .name_common = "Scinque crocodile de Nouvelle-Guinée",
        .adult_size_mm = 90,
        .lifespan_years = 12,
        .rarity = 5,
        .difficulty = 7,
        .temp_optimal_min = 23,
        .temp_optimal_max = 27,
        .humidity_optimal = 85,
        .sexual_maturity_days = 730,
        .clutch_size = 1,
        .incubation_days = 65
    },

    // T. ponceleti
    {
        .id = SPECIES_T_PONCELETI,
        .name_latin = "Tribolonotus ponceleti",
        .name_common = "Scinque crocodile de Poncelet",
        .adult_size_mm = 75,
        .lifespan_years = 8,
        .rarity = 6,
        .difficulty = 7,
        .temp_optimal_min = 24,
        .temp_optimal_max = 28,
        .humidity_optimal = 82,
        .sexual_maturity_days = 700,
        .clutch_size = 1,
        .incubation_days = 58
    },

    // T. psychosauropus
    {
        .id = SPECIES_T_PSYCHOSAUROPUS,
        .name_latin = "Tribolonotus psychosauropus",
        .name_common = "Scinque crocodile psychédélique",
        .adult_size_mm = 85,
        .lifespan_years = 10,
        .rarity = 8,
        .difficulty = 8,
        .temp_optimal_min = 23,
        .temp_optimal_max = 26,
        .humidity_optimal = 88,
        .sexual_maturity_days = 800,
        .clutch_size = 1,
        .incubation_days = 70
    },

    // T. pseudoponceleti
    {
        .id = SPECIES_T_PSEUDOPONCELETI,
        .name_latin = "Tribolonotus pseudoponceleti",
        .name_common = "Faux scinque de Poncelet",
        .adult_size_mm = 78,
        .lifespan_years = 9,
        .rarity = 7,
        .difficulty = 7,
        .temp_optimal_min = 24,
        .temp_optimal_max = 28,
        .humidity_optimal = 83,
        .sexual_maturity_days = 750,
        .clutch_size = 1,
        .incubation_days = 62
    },

    // T. brongersmai
    {
        .id = SPECIES_T_BRONGERSMAI,
        .name_latin = "Tribolonotus brongersmai",
        .name_common = "Scinque crocodile de Brongersma",
        .adult_size_mm = 95,
        .lifespan_years = 11,
        .rarity = 6,
        .difficulty = 7,
        .temp_optimal_min = 23,
        .temp_optimal_max = 27,
        .humidity_optimal = 84,
        .sexual_maturity_days = 760,
        .clutch_size = 1,
        .incubation_days = 64
    },

    // T. annectens
    {
        .id = SPECIES_T_ANNECTENS,
        .name_latin = "Tribolonotus annectens",
        .name_common = "Scinque crocodile intermédiaire",
        .adult_size_mm = 82,
        .lifespan_years = 9,
        .rarity = 5,
        .difficulty = 6,
        .temp_optimal_min = 24,
        .temp_optimal_max = 28,
        .humidity_optimal = 81,
        .sexual_maturity_days = 720,
        .clutch_size = 1,
        .incubation_days = 60
    },

    // T. parkeri
    {
        .id = SPECIES_T_PARKERI,
        .name_latin = "Tribolonotus parkeri",
        .name_common = "Scinque crocodile de Parker",
        .adult_size_mm = 88,
        .lifespan_years = 10,
        .rarity = 9,
        .difficulty = 9,
        .temp_optimal_min = 22,
        .temp_optimal_max = 26,
        .humidity_optimal = 90,
        .sexual_maturity_days = 850,
        .clutch_size = 1,
        .incubation_days = 75
    },

    // T. blanchardi
    {
        .id = SPECIES_T_BLANCHARDI,
        .name_latin = "Tribolonotus blanchardi",
        .name_common = "Scinque crocodile de Blanchard",
        .adult_size_mm = 92,
        .lifespan_years = 11,
        .rarity = 7,
        .difficulty = 8,
        .temp_optimal_min = 23,
        .temp_optimal_max = 27,
        .humidity_optimal = 86,
        .sexual_maturity_days = 780,
        .clutch_size = 1,
        .incubation_days = 66
    },

    // T. schmidti
    {
        .id = SPECIES_T_SCHMIDTI,
        .name_latin = "Tribolonotus schmidti",
        .name_common = "Scinque crocodile de Schmidt",
        .adult_size_mm = 86,
        .lifespan_years = 10,
        .rarity = 8,
        .difficulty = 8,
        .temp_optimal_min = 23,
        .temp_optimal_max = 27,
        .humidity_optimal = 87,
        .sexual_maturity_days = 800,
        .clutch_size = 1,
        .incubation_days = 68
    }
};

#endif // TRIBOLONOTUS_TYPES_H
