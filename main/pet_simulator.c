/**
 * @file pet_simulator.c
 * @brief Implémentation du moteur de simulation d'élevage de Tribolonotus
 */

#include "pet_simulator.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "PET_SIM";
static const char *NVS_NAMESPACE = "pet_save";

// ====================================================================================
// VARIABLES GLOBALES
// ====================================================================================

static game_state_t g_game_state = {0};
static inventory_t g_inventory = {0};
static bool g_simulator_initialized = false;

// ====================================================================================
// PROTOTYPES INTERNES
// ====================================================================================

static uint32_t get_current_time(void);
static uint8_t clamp_u8(int16_t value, uint8_t min, uint8_t max);
static void pet_init_default(pet_t *pet, tribolonotus_species_t species, const char *name, sex_t sex);

// ====================================================================================
// INITIALISATION
// ====================================================================================

void pet_simulator_init(void) {
    ESP_LOGI(TAG, "Initialisation du simulateur de Tribolonotus...");

    // Tentative de chargement depuis NVS
    if (!pet_simulator_load()) {
        ESP_LOGW(TAG, "Aucune sauvegarde trouvée, nouvelle partie");
        pet_simulator_reset();
    }

    g_simulator_initialized = true;
    ESP_LOGI(TAG, "Simulateur initialisé : %d lézards actifs", g_game_state.pet_count);
}

void pet_simulator_reset(void) {
    ESP_LOGI(TAG, "Réinitialisation complète du jeu");

    memset(&g_game_state, 0, sizeof(game_state_t));
    memset(&g_inventory, 0, sizeof(inventory_t));

    g_game_state.game_start_time = get_current_time();
    g_game_state.player_money = 500; // 500$ au départ
    g_game_state.next_pet_id = 1;

    // Inventaire de départ
    g_inventory.crickets = 20;
    g_inventory.dubias = 10;
    g_inventory.waxworms = 5;
    g_inventory.isopods = 10;
    g_inventory.earthworms = 5;
    g_inventory.vitamin_powder = 3;
    g_inventory.calcium_powder = 3;
    g_inventory.medications = 1;

    // Créer le premier lézard (T. gracilis)
    pet_create(SPECIES_T_GRACILIS, "Ruby", SEX_UNKNOWN);

    pet_simulator_save();
}

// ====================================================================================
// MISE À JOUR GLOBALE
// ====================================================================================

void pet_simulator_update(void) {
    if (!g_simulator_initialized) {
        return;
    }

    uint32_t current_time = get_current_time();

    // Parcourir tous les lézards
    for (uint8_t i = 0; i < MAX_PETS; i++) {
        pet_t *pet = &g_game_state.pets[i];

        if (!pet->is_alive) {
            continue;
        }

        // Calculer temps écoulé depuis dernière MAJ
        uint32_t elapsed = current_time - pet->last_update;
        pet->last_update = current_time;

        // Mises à jour
        pet_update_needs(pet, elapsed);
        pet_update_growth(pet);
        pet_update_size(pet);
        pet_update_health(pet);

        // Vérifier décès
        if (pet_check_death(pet)) {
            ESP_LOGW(TAG, "%s est décédé(e) :(", pet->name);
        }

        // Mue aléatoire (1% chance par heure)
        if (!pet->health.is_shedding && (esp_random() % 360) == 0) {
            pet_trigger_shedding(pet);
        }
    }

    // Mettre à jour temps de jeu
    g_game_state.total_playtime_seconds++;

    // Sauvegarde auto toutes les 5 minutes
    if (g_game_state.total_playtime_seconds % 300 == 0) {
        ESP_LOGI(TAG, "Sauvegarde automatique...");
        pet_simulator_save();
    }
}

// ====================================================================================
// GESTION DES LÉZARDS
// ====================================================================================

int8_t pet_create(tribolonotus_species_t species, const char *name, sex_t sex) {
    if (g_game_state.pet_count >= MAX_PETS) {
        ESP_LOGW(TAG, "Impossible de créer : limite atteinte (%d)", MAX_PETS);
        return -1;
    }

    // Trouver slot libre
    int8_t index = -1;
    for (uint8_t i = 0; i < MAX_PETS; i++) {
        if (!g_game_state.pets[i].is_alive) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        return -1;
    }

    pet_t *pet = &g_game_state.pets[index];
    pet_init_default(pet, species, name, sex);

    g_game_state.pet_count++;
    ESP_LOGI(TAG, "Nouveau lézard créé : %s (%s)", name, SPECIES_DATA[species].name_common);

    return index;
}

void pet_remove(uint8_t pet_index) {
    if (pet_index >= MAX_PETS) {
        return;
    }

    pet_t *pet = &g_game_state.pets[pet_index];
    if (!pet->is_alive) {
        return;
    }

    ESP_LOGI(TAG, "Suppression de %s", pet->name);
    pet->is_alive = false;
    g_game_state.pet_count--;
}

pet_t *pet_get(uint8_t pet_index) {
    if (pet_index >= MAX_PETS) {
        return NULL;
    }

    pet_t *pet = &g_game_state.pets[pet_index];
    return pet->is_alive ? pet : NULL;
}

pet_t *pet_get_current(void) {
    return pet_get(g_game_state.current_pet_index);
}

void pet_set_current(uint8_t pet_index) {
    if (pet_index < MAX_PETS && g_game_state.pets[pet_index].is_alive) {
        g_game_state.current_pet_index = pet_index;
    }
}

uint8_t pet_get_current_index(void) {
    return g_game_state.current_pet_index;
}

uint8_t pet_get_count(void) {
    return g_game_state.pet_count;
}

// ====================================================================================
// ACTIONS DU JOUEUR
// ====================================================================================

bool pet_feed(uint8_t pet_index, food_type_t food_type) {
    pet_t *pet = pet_get(pet_index);
    if (!pet) {
        return false;
    }

    // Vérifier inventaire
    uint16_t *food_count = NULL;
    uint8_t hunger_reduction = 0;
    uint8_t happiness_boost = 0;

    switch (food_type) {
    case FOOD_CRICKET:
        food_count = &g_inventory.crickets;
        hunger_reduction = 20;
        happiness_boost = 5;
        break;
    case FOOD_DUBIA:
        food_count = &g_inventory.dubias;
        hunger_reduction = 25;
        happiness_boost = 8;
        break;
    case FOOD_WAXWORM:
        food_count = &g_inventory.waxworms;
        hunger_reduction = 30;
        happiness_boost = 15; // Très apprécié !
        break;
    case FOOD_ISOPOD:
        food_count = &g_inventory.isopods;
        hunger_reduction = 15;
        happiness_boost = 10;
        break;
    case FOOD_EARTHWORM:
        food_count = &g_inventory.earthworms;
        hunger_reduction = 28;
        happiness_boost = 12;
        break;
    default:
        return false;
    }

    if (*food_count == 0) {
        ESP_LOGW(TAG, "Plus de nourriture de ce type !");
        return false;
    }

    // Consommer
    (*food_count)--;
    pet->needs.hunger = (pet->needs.hunger > hunger_reduction) ? pet->needs.hunger - hunger_reduction : 0;
    pet->needs.happiness = clamp_u8(pet->needs.happiness + happiness_boost, 0, 100);
    pet->last_fed = get_current_time();
    pet->stats.total_feeds++;

    ESP_LOGI(TAG, "%s a mangé ! Faim: %d%%", pet->name, pet->needs.hunger);
    return true;
}

bool pet_water(uint8_t pet_index) {
    pet_t *pet = pet_get(pet_index);
    if (!pet) {
        return false;
    }

    pet->needs.thirst = 0; // Complètement hydraté
    pet->needs.happiness = clamp_u8(pet->needs.happiness + 3, 0, 100);
    pet->last_watered = get_current_time();

    ESP_LOGI(TAG, "%s a bu de l'eau", pet->name);
    return true;
}

bool pet_heat(uint8_t pet_index, uint8_t duration_minutes) {
    pet_t *pet = pet_get(pet_index);
    if (!pet) {
        return false;
    }

    // Augmente température en fonction de la durée
    uint8_t temp_boost = duration_minutes * 2;
    pet->needs.temperature = clamp_u8(pet->needs.temperature + temp_boost, 0, 100);

    ESP_LOGI(TAG, "%s profite de la chaleur", pet->name);
    return true;
}

bool pet_mist(uint8_t pet_index) {
    pet_t *pet = pet_get(pet_index);
    if (!pet) {
        return false;
    }

    pet->needs.humidity = clamp_u8(pet->needs.humidity + 30, 0, 100);
    pet->needs.happiness = clamp_u8(pet->needs.happiness + 5, 0, 100);

    ESP_LOGI(TAG, "%s apprécie la brumisation", pet->name);
    return true;
}

bool pet_clean(uint8_t pet_index) {
    pet_t *pet = pet_get(pet_index);
    if (!pet) {
        return false;
    }

    pet->needs.cleanliness = 100; // Terrarium impeccable
    pet->needs.happiness = clamp_u8(pet->needs.happiness + 10, 0, 100);
    pet->health.health_points = clamp_u8(pet->health.health_points + 5, 0, 100);
    pet->last_cleaned = get_current_time();

    ESP_LOGI(TAG, "Terrarium de %s nettoyé", pet->name);
    return true;
}

bool pet_play(uint8_t pet_index) {
    pet_t *pet = pet_get(pet_index);
    if (!pet) {
        return false;
    }

    // Jouer augmente bonheur mais consomme énergie
    pet->needs.happiness = clamp_u8(pet->needs.happiness + 20, 0, 100);
    pet->needs.energy = (pet->needs.energy > 10) ? pet->needs.energy - 10 : 0;
    pet->last_interaction = get_current_time();
    pet->stats.total_interactions++;

    ESP_LOGI(TAG, "Interaction avec %s (+20 bonheur)", pet->name);
    return true;
}

bool pet_heal(uint8_t pet_index) {
    pet_t *pet = pet_get(pet_index);
    if (!pet || g_inventory.medications == 0) {
        return false;
    }

    g_inventory.medications--;
    pet->health.health_points = 100;
    pet->health.status = HEALTH_EXCELLENT;
    pet->health.last_vet_visit = get_current_time();

    ESP_LOGI(TAG, "%s a reçu des soins vétérinaires", pet->name);
    return true;
}

bool pet_breed(uint8_t female_index, uint8_t male_index) {
    pet_t *female = pet_get(female_index);
    pet_t *male = pet_get(male_index);

    if (!female || !male) {
        return false;
    }

    // Vérifications
    if (female->sex != SEX_FEMALE || male->sex != SEX_MALE) {
        ESP_LOGW(TAG, "Sexes incompatibles pour reproduction");
        return false;
    }

    if (female->species != male->species) {
        ESP_LOGW(TAG, "Espèces différentes, pas de reproduction");
        return false;
    }

    const species_info_t *info = &SPECIES_DATA[female->species];

    if (female->stats.age_days < info->sexual_maturity_days || male->stats.age_days < info->sexual_maturity_days) {
        ESP_LOGW(TAG, "Lézards trop jeunes pour se reproduire");
        return false;
    }

    if (female->health.is_pregnant) {
        ESP_LOGW(TAG, "Femelle déjà gravide");
        return false;
    }

    // Succès ! (probabilité 70%)
    if ((esp_random() % 100) < 70) {
        female->health.is_pregnant = true;
        female->health.days_until_eggs = info->incubation_days;

        ESP_LOGI(TAG, "Reproduction réussie ! %s est gravide", female->name);
        return true;
    }

    ESP_LOGI(TAG, "Reproduction échouée, réessayez plus tard");
    return false;
}

// ====================================================================================
// SYSTÈME DE CROISSANCE
// ====================================================================================

void pet_update_growth(pet_t *pet) {
    uint32_t current_time = get_current_time();
    pet->stats.age_days = (current_time - pet->birth_timestamp) / 86400; // 86400 sec = 1 jour

    const species_info_t *info = &SPECIES_DATA[pet->species];

    // Déterminer stade
    growth_stage_t old_stage = pet->stage;

    if (pet->stats.age_days < 60) {
        pet->stage = STAGE_EGG;
    } else if (pet->stats.age_days < 180) {
        pet->stage = STAGE_HATCHLING;
    } else if (pet->stats.age_days < 365) {
        pet->stage = STAGE_JUVENILE;
    } else if (pet->stats.age_days < 730) {
        pet->stage = STAGE_SUBADULT;
    } else {
        pet->stage = STAGE_ADULT;
    }

    // Log transition
    if (pet->stage != old_stage) {
        ESP_LOGI(TAG, "%s a évolué en %s !", pet->name, pet_stage_to_string(pet->stage));
    }

    // Déterminer sexe si inconnu (à partir du stade sub-adulte)
    if (pet->sex == SEX_UNKNOWN && pet->stage >= STAGE_SUBADULT) {
        pet_determine_sex(pet);
    }
}

void pet_update_size(pet_t *pet) {
    const species_info_t *info = &SPECIES_DATA[pet->species];

    // Croissance progressive jusqu'à taille adulte
    float growth_factor = 0.0f;

    switch (pet->stage) {
    case STAGE_EGG:
        growth_factor = 0.05f;
        break;
    case STAGE_HATCHLING:
        growth_factor = 0.3f;
        break;
    case STAGE_JUVENILE:
        growth_factor = 0.6f;
        break;
    case STAGE_SUBADULT:
        growth_factor = 0.85f;
        break;
    case STAGE_ADULT:
        growth_factor = 1.0f;
        break;
    default:
        growth_factor = 1.0f;
    }

    pet->stats.length_mm = (uint16_t)(info->adult_size_mm * growth_factor);

    // Poids approximatif (en grammes, proportionnel à la taille)
    pet->stats.weight_grams = (uint16_t)(pet->stats.length_mm * 0.5f); // Estimation simple
}

void pet_determine_sex(pet_t *pet) {
    if (pet->sex != SEX_UNKNOWN) {
        return;
    }

    // 50/50 mâle/femelle
    pet->sex = (esp_random() % 2) ? SEX_MALE : SEX_FEMALE;
    ESP_LOGI(TAG, "%s est un(e) %s !", pet->name, pet->sex == SEX_MALE ? "mâle" : "femelle");
}

// ====================================================================================
// SYSTÈME DE BESOINS
// ====================================================================================

void pet_update_needs(pet_t *pet, uint32_t elapsed_seconds) {
    // Convertir secondes en minutes
    float elapsed_minutes = elapsed_seconds / 60.0f;

    // Décroissance des besoins
    pet->needs.hunger = clamp_u8(pet->needs.hunger + (HUNGER_DECAY_RATE * elapsed_minutes), 0, 100);
    pet->needs.thirst = clamp_u8(pet->needs.thirst + (THIRST_DECAY_RATE * elapsed_minutes), 0, 100);
    pet->needs.temperature = (pet->needs.temperature > TEMPERATURE_DECAY_RATE * elapsed_minutes) ? pet->needs.temperature - (TEMPERATURE_DECAY_RATE * elapsed_minutes) : 0;
    pet->needs.humidity = (pet->needs.humidity > HUMIDITY_DECAY_RATE * elapsed_minutes) ? pet->needs.humidity - (HUMIDITY_DECAY_RATE * elapsed_minutes) : 0;
    pet->needs.cleanliness = (pet->needs.cleanliness > CLEANLINESS_DECAY_RATE * elapsed_minutes) ? pet->needs.cleanliness - (CLEANLINESS_DECAY_RATE * elapsed_minutes) : 0;

    // Bonheur dépend des autres besoins
    pet->mood = pet_calculate_mood(pet);

    // Énergie (cycle jour/nuit simple)
    uint32_t time_of_day = get_current_time() % 86400; // Secondes depuis minuit
    if (time_of_day > 21600 && time_of_day < 72000) {  // 6h - 20h
        pet->needs.energy = clamp_u8(pet->needs.energy + 1, 0, 100); // Récupère énergie de jour
    } else {
        pet->needs.energy = (pet->needs.energy > 1) ? pet->needs.energy - 1 : 0; // Fatigue la nuit
    }
}

mood_t pet_calculate_mood(const pet_t *pet) {
    // Calculer score moyen des besoins (inversé pour faim/soif)
    int16_t total = 0;
    total += (100 - pet->needs.hunger);
    total += (100 - pet->needs.thirst);
    total += pet->needs.temperature;
    total += pet->needs.humidity;
    total += pet->needs.cleanliness;
    total += pet->needs.happiness;

    uint8_t avg = total / 6;

    // Mapper sur humeur
    if (avg < 20)
        return MOOD_DEPRESSED;
    if (avg < 40)
        return MOOD_SAD;
    if (avg < 60)
        return MOOD_NEUTRAL;
    if (avg < 75)
        return MOOD_CONTENT;
    if (avg < 90)
        return MOOD_HAPPY;
    return MOOD_ECSTATIC;
}

health_status_t pet_calculate_health(const pet_t *pet) {
    if (pet->health.health_points >= 90)
        return HEALTH_EXCELLENT;
    if (pet->health.health_points >= 70)
        return HEALTH_GOOD;
    if (pet->health.health_points >= 50)
        return HEALTH_WEAK;
    if (pet->health.health_points >= 30)
        return HEALTH_SICK;
    if (pet->health.health_points > 0)
        return HEALTH_CRITICAL;
    return HEALTH_DEAD;
}

// ====================================================================================
// SYSTÈME DE SANTÉ
// ====================================================================================

void pet_update_health(pet_t *pet) {
    int8_t health_change = 0;

    // Pénalités si besoins critiques
    if (pet->needs.hunger > 80)
        health_change -= 2;
    if (pet->needs.thirst > 80)
        health_change -= 3;
    if (pet->needs.temperature < 30)
        health_change -= 2;
    if (pet->needs.humidity < 30)
        health_change -= 1;
    if (pet->needs.cleanliness < 20)
        health_change -= 1;

    // Bonus si tous besoins OK
    if (pet->mood >= MOOD_HAPPY) {
        health_change += 1;
    }

    // Appliquer changement
    pet->health.health_points = clamp_u8(pet->health.health_points + health_change, 0, 100);
    pet->health.status = pet_calculate_health(pet);

    // Gestation
    if (pet->health.is_pregnant) {
        if (pet->health.days_until_eggs > 0) {
            pet->health.days_until_eggs--;
        } else {
            // Ponte !
            ESP_LOGI(TAG, "%s a pondu un œuf !", pet->name);
            const species_info_t *info = &SPECIES_DATA[pet->species];
            sex_t baby_sex = SEX_UNKNOWN;

            char baby_name[PET_NAME_MAX_LEN];
            snprintf(baby_name, sizeof(baby_name), "Bébé-%d", g_game_state.next_pet_id);

            if (pet_create(pet->species, baby_name, baby_sex) >= 0) {
                pet->stats.offspring_count++;
            }

            pet->health.is_pregnant = false;
        }
    }
}

bool pet_check_death(pet_t *pet) {
    if (pet->health.health_points == 0 || pet->health.status == HEALTH_DEAD) {
        pet->is_alive = false;
        pet->health.status = HEALTH_DEAD;
        g_game_state.pet_count--;
        return true;
    }

    return false;
}

void pet_trigger_shedding(pet_t *pet) {
    pet->health.is_shedding = true;
    ESP_LOGI(TAG, "%s commence sa mue", pet->name);

    // La mue dure quelques jours
    // (On pourrait ajouter un timer pour la terminer automatiquement)
}

// ====================================================================================
// INVENTAIRE ET ARGENT
// ====================================================================================

inventory_t *pet_get_inventory(void) {
    return &g_inventory;
}

bool shop_buy_food(food_type_t food_type, uint16_t quantity) {
    uint32_t price_per_unit = 0;
    uint16_t *food_count = NULL;

    switch (food_type) {
    case FOOD_CRICKET:
        price_per_unit = 1;
        food_count = &g_inventory.crickets;
        break;
    case FOOD_DUBIA:
        price_per_unit = 2;
        food_count = &g_inventory.dubias;
        break;
    case FOOD_WAXWORM:
        price_per_unit = 3;
        food_count = &g_inventory.waxworms;
        break;
    case FOOD_ISOPOD:
        price_per_unit = 2;
        food_count = &g_inventory.isopods;
        break;
    case FOOD_EARTHWORM:
        price_per_unit = 2;
        food_count = &g_inventory.earthworms;
        break;
    default:
        return false;
    }

    uint32_t total_cost = price_per_unit * quantity;

    if (g_game_state.player_money < total_cost) {
        ESP_LOGW(TAG, "Pas assez d'argent (besoin $%lu)", total_cost);
        return false;
    }

    g_game_state.player_money -= total_cost;
    *food_count += quantity;

    ESP_LOGI(TAG, "Acheté %d unités pour $%lu", quantity, total_cost);
    return true;
}

uint32_t pet_get_money(void) {
    return g_game_state.player_money;
}

void pet_add_money(uint32_t amount) {
    g_game_state.player_money += amount;
}

bool pet_remove_money(uint32_t amount) {
    if (g_game_state.player_money < amount) {
        return false;
    }
    g_game_state.player_money -= amount;
    return true;
}

// ====================================================================================
// SAUVEGARDE/CHARGEMENT NVS
// ====================================================================================

void pet_simulator_save(void) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ouverture NVS: %s", esp_err_to_name(err));
        return;
    }

    // Sauvegarder état du jeu
    nvs_set_blob(handle, "game_state", &g_game_state, sizeof(game_state_t));
    nvs_set_blob(handle, "inventory", &g_inventory, sizeof(inventory_t));

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Partie sauvegardée (NVS)");
}

bool pet_simulator_load(void) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required_size = sizeof(game_state_t);
    err = nvs_get_blob(handle, "game_state", &g_game_state, &required_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    required_size = sizeof(inventory_t);
    err = nvs_get_blob(handle, "inventory", &g_inventory, &required_size);

    nvs_close(handle);

    ESP_LOGI(TAG, "Partie chargée depuis NVS");
    return true;
}

// ====================================================================================
// UTILITAIRES
// ====================================================================================

const species_info_t *pet_get_species_info(tribolonotus_species_t species) {
    if (species >= SPECIES_COUNT) {
        return NULL;
    }
    return &SPECIES_DATA[species];
}

const char *pet_stage_to_string(growth_stage_t stage) {
    static const char *stages[] = {"Œuf", "Nouveau-né", "Juvénile", "Sub-adulte", "Adulte"};
    if (stage >= STAGE_COUNT)
        return "Inconnu";
    return stages[stage];
}

const char *pet_health_to_string(health_status_t health) {
    static const char *healths[] = {"Mort", "Critique", "Malade", "Faible", "Bonne", "Excellente"};
    if (health >= HEALTH_COUNT)
        return "Inconnu";
    return healths[health];
}

const char *pet_mood_to_string(mood_t mood) {
    static const char *moods[] = {"Déprimé", "Triste", "Neutre", "Content", "Heureux", "Extatique"};
    if (mood >= MOOD_COUNT)
        return "Inconnu";
    return moods[mood];
}

uint32_t pet_get_playtime(void) {
    return g_game_state.total_playtime_seconds;
}

// ====================================================================================
// FONCTIONS INTERNES
// ====================================================================================

static uint32_t get_current_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}

static uint8_t clamp_u8(int16_t value, uint8_t min, uint8_t max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return (uint8_t)value;
}

static void pet_init_default(pet_t *pet, tribolonotus_species_t species, const char *name, sex_t sex) {
    memset(pet, 0, sizeof(pet_t));

    strncpy(pet->name, name, PET_NAME_MAX_LEN - 1);
    pet->species = species;
    pet->sex = sex;
    pet->stage = STAGE_EGG;
    pet->birth_timestamp = get_current_time();
    pet->id = g_game_state.next_pet_id++;

    // Besoins initiaux (œuf en bonne santé)
    pet->needs.hunger = 10;
    pet->needs.thirst = 10;
    pet->needs.temperature = 80;
    pet->needs.humidity = 90;
    pet->needs.cleanliness = 100;
    pet->needs.happiness = 70;
    pet->needs.energy = 50;

    // Santé
    pet->health.status = HEALTH_EXCELLENT;
    pet->health.health_points = 100;
    pet->health.is_shedding = false;
    pet->health.is_pregnant = false;

    // Timestamps
    uint32_t now = get_current_time();
    pet->last_fed = now;
    pet->last_watered = now;
    pet->last_cleaned = now;
    pet->last_interaction = now;
    pet->last_update = now;

    pet->is_alive = true;
    pet->is_selected = false;

    // Variante de couleur aléatoire
    pet->color_variant = esp_random() % 256;
}
