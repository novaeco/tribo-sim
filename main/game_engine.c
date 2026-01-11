/**
 * @file game_engine.c
 * @brief Tribomon Game Engine Implementation
 */

#include "game_engine.h"
#include "tribomon_types.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <math.h>

static const char *TAG = "GAME_ENGINE";

// Global game state
static Game g_game = {0};
static bool g_initialized = false;

// ====================================================================================
// TYPE EFFECTIVENESS TABLE
// ====================================================================================

// Type chart: [attacker][defender] = effectiveness
// Multiply by 10 to avoid floating point (20 = 2.0x, 10 = 1.0x, 5 = 0.5x, 0 = 0.0x)
static const uint8_t TYPE_CHART[TYPE_COUNT][TYPE_COUNT] = {
    // Defender:  NOR  FIR  WAT  GRA  ELE  ICE  FIG  POI  GRO  FLY  PSY  BUG  ROC  GHO  DRA  DAR  STE  FAI
    /* NORMAL */  {10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,   5,   0,  10,  10,   5,  10},
    /* FIRE */    {10,   5,   5,  20,  10,  20,  10,  10,  10,  10,  10,  20,   5,  10,   5,  10,  20,  10},
    /* WATER */   {10,  20,   5,   5,  10,  10,  10,  10,  20,  10,  10,  10,  20,  10,   5,  10,  10,  10},
    /* GRASS */   {10,   5,  20,   5,  10,  10,  10,   5,  20,   5,  10,   5,  20,  10,   5,  10,   5,  10},
    /* ELECTRIC */{10,  10,  20,   5,   5,  10,  10,  10,   0,  20,  10,  10,  10,  10,   5,  10,  10,  10},
    /* ICE */     {10,   5,   5,  20,  10,   5,  10,  10,  20,  20,  10,  10,  10,  10,  20,  10,   5,  10},
    /* FIGHTING */{20,  10,  10,  10,  10,  20,  10,   5,  10,   5,   5,   5,  20,   0,  10,  20,  20,   5},
    /* POISON */  {10,  10,  10,  20,  10,  10,  10,   5,   5,  10,  10,  10,   5,   5,  10,  10,   0,  20},
    /* GROUND */  {10,  20,  10,   5,  20,  10,  10,  20,  10,   0,  10,   5,  20,  10,  10,  10,  20,  10},
    /* FLYING */  {10,  10,  10,  20,   5,  10,  20,  10,  10,  10,  10,  20,   5,  10,  10,  10,   5,  10},
    /* PSYCHIC */ {10,  10,  10,  10,  10,  10,  20,  20,  10,  10,   5,  10,  10,  10,  10,   0,   5,  10},
    /* BUG */     {10,   5,  10,  20,  10,  10,   5,   5,  10,   5,  20,  10,  10,   5,  10,  20,   5,   5},
    /* ROCK */    {10,  20,  10,  10,  10,  20,   5,  10,   5,  20,  10,  20,  10,  10,  10,  10,   5,  10},
    /* GHOST */   {0,   10,  10,  10,  10,  10,  10,  10,  10,  10,  20,  10,  10,  20,  10,   5,  10,  10},
    /* DRAGON */  {10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  20,  10,   5,   0},
    /* DARK */    {10,  10,  10,  10,  10,  10,   5,  10,  10,  10,  20,  10,  10,  20,  10,   5,  10,   5},
    /* STEEL */   {10,   5,   5,  10,   5,  20,  10,  10,  10,  10,  10,  10,  20,  10,  10,  10,   5,  20},
    /* FAIRY */   {10,   5,  10,  10,  10,  10,  20,   5,  10,  10,  10,  10,  10,  10,  20,  20,   5,  10}
};

// ====================================================================================
// TRIBOMON SPECIES DATABASE
// ====================================================================================

static const TribomonSpecies SPECIES_DATABASE[] = {
    // Starter: Fire type
    {
        .id = 1,
        .name = "Flamby",
        .type1 = TYPE_FIRE,
        .type2 = TYPE_NORMAL,
        .base_stats = {39, 52, 43, 60, 50, 65},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 62,
        .evolves_to = 2,
        .evolve_level = 16
    },
    {
        .id = 2,
        .name = "Infernix",
        .type1 = TYPE_FIRE,
        .type2 = TYPE_NORMAL,
        .base_stats = {58, 64, 58, 80, 65, 80},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 142,
        .evolves_to = 3,
        .evolve_level = 36
    },
    {
        .id = 3,
        .name = "Pyroclaw",
        .type1 = TYPE_FIRE,
        .type2 = TYPE_DRAGON,
        .base_stats = {78, 84, 78, 109, 85, 100},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 240,
        .evolves_to = 0,
        .evolve_level = 0
    },

    // Starter: Water type
    {
        .id = 4,
        .name = "Aquario",
        .type1 = TYPE_WATER,
        .type2 = TYPE_NORMAL,
        .base_stats = {44, 48, 65, 50, 64, 43},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 63,
        .evolves_to = 5,
        .evolve_level = 16
    },
    {
        .id = 5,
        .name = "Torrento",
        .type1 = TYPE_WATER,
        .type2 = TYPE_NORMAL,
        .base_stats = {59, 63, 80, 65, 80, 58},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 142,
        .evolves_to = 6,
        .evolve_level = 36
    },
    {
        .id = 6,
        .name = "Hydroking",
        .type1 = TYPE_WATER,
        .type2 = TYPE_ICE,
        .base_stats = {79, 83, 100, 85, 105, 78},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 239,
        .evolves_to = 0,
        .evolve_level = 0
    },

    // Starter: Grass type
    {
        .id = 7,
        .name = "Leafo",
        .type1 = TYPE_GRASS,
        .type2 = TYPE_POISON,
        .base_stats = {45, 49, 49, 65, 65, 45},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 64,
        .evolves_to = 8,
        .evolve_level = 16
    },
    {
        .id = 8,
        .name = "Vinespike",
        .type1 = TYPE_GRASS,
        .type2 = TYPE_POISON,
        .base_stats = {60, 62, 63, 80, 80, 60},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 142,
        .evolves_to = 9,
        .evolve_level = 32
    },
    {
        .id = 9,
        .name = "Florathorn",
        .type1 = TYPE_GRASS,
        .type2 = TYPE_POISON,
        .base_stats = {80, 82, 83, 100, 100, 80},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 236,
        .evolves_to = 0,
        .evolve_level = 0
    },

    // Early game common
    {
        .id = 10,
        .name = "Sparkrat",
        .type1 = TYPE_ELECTRIC,
        .type2 = TYPE_NORMAL,
        .base_stats = {35, 55, 40, 50, 50, 90},
        .capture_rate = 190,
        .growth_rate = GROWTH_MEDIUM_FAST,
        .base_exp_yield = 112,
        .evolves_to = 11,
        .evolve_level = 20
    },
    {
        .id = 11,
        .name = "Voltmouse",
        .type1 = TYPE_ELECTRIC,
        .type2 = TYPE_NORMAL,
        .base_stats = {60, 90, 55, 90, 80, 110},
        .capture_rate = 75,
        .growth_rate = GROWTH_MEDIUM_FAST,
        .base_exp_yield = 218,
        .evolves_to = 0,
        .evolve_level = 0
    },

    // Flying type
    {
        .id = 12,
        .name = "Skyling",
        .type1 = TYPE_FLYING,
        .type2 = TYPE_NORMAL,
        .base_stats = {40, 45, 40, 35, 35, 56},
        .capture_rate = 255,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 50,
        .evolves_to = 13,
        .evolve_level = 18
    },
    {
        .id = 13,
        .name = "Aerowing",
        .type1 = TYPE_FLYING,
        .type2 = TYPE_NORMAL,
        .base_stats = {63, 60, 55, 50, 50, 71},
        .capture_rate = 120,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 122,
        .evolves_to = 14,
        .evolve_level = 36
    },
    {
        .id = 14,
        .name = "Stormbeak",
        .type1 = TYPE_FLYING,
        .type2 = TYPE_DRAGON,
        .base_stats = {83, 80, 75, 70, 70, 101},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 216,
        .evolves_to = 0,
        .evolve_level = 0
    },

    // Bug types
    {
        .id = 15,
        .name = "Beetlet",
        .type1 = TYPE_BUG,
        .type2 = TYPE_NORMAL,
        .base_stats = {40, 35, 30, 20, 20, 50},
        .capture_rate = 255,
        .growth_rate = GROWTH_MEDIUM_FAST,
        .base_exp_yield = 39,
        .evolves_to = 16,
        .evolve_level = 7
    },
    {
        .id = 16,
        .name = "Cocoonix",
        .type1 = TYPE_BUG,
        .type2 = TYPE_NORMAL,
        .base_stats = {50, 20, 55, 25, 25, 30},
        .capture_rate = 120,
        .growth_rate = GROWTH_MEDIUM_FAST,
        .base_exp_yield = 72,
        .evolves_to = 17,
        .evolve_level = 10
    },
    {
        .id = 17,
        .name = "Butterfury",
        .type1 = TYPE_BUG,
        .type2 = TYPE_FLYING,
        .base_stats = {60, 45, 50, 90, 80, 70},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_FAST,
        .base_exp_yield = 178,
        .evolves_to = 0,
        .evolve_level = 0
    },

    // Rock/Ground
    {
        .id = 18,
        .name = "Rocklet",
        .type1 = TYPE_ROCK,
        .type2 = TYPE_GROUND,
        .base_stats = {40, 80, 100, 30, 30, 20},
        .capture_rate = 255,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 60,
        .evolves_to = 19,
        .evolve_level = 25
    },
    {
        .id = 19,
        .name = "Bouldron",
        .type1 = TYPE_ROCK,
        .type2 = TYPE_GROUND,
        .base_stats = {55, 95, 115, 45, 45, 35},
        .capture_rate = 120,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 137,
        .evolves_to = 20,
        .evolve_level = 38
    },
    {
        .id = 20,
        .name = "Titanolith",
        .type1 = TYPE_ROCK,
        .type2 = TYPE_STEEL,
        .base_stats = {80, 120, 130, 55, 65, 45},
        .capture_rate = 45,
        .growth_rate = GROWTH_MEDIUM_SLOW,
        .base_exp_yield = 223,
        .evolves_to = 0,
        .evolve_level = 0
    }
};

#define SPECIES_COUNT (sizeof(SPECIES_DATABASE) / sizeof(TribomonSpecies))

// ====================================================================================
// ATTACK DATABASE
// ====================================================================================

static const Attack ATTACK_DATABASE[] = {
    // Normal attacks
    {0, "Tackle", TYPE_NORMAL, CATEGORY_PHYSICAL, 40, 100, 35, 0, STATUS_NONE, {0}},
    {1, "Scratch", TYPE_NORMAL, CATEGORY_PHYSICAL, 40, 100, 35, 0, STATUS_NONE, {0}},
    {2, "Quick Attack", TYPE_NORMAL, CATEGORY_PHYSICAL, 40, 100, 30, 0, STATUS_NONE, {0}},
    {3, "Body Slam", TYPE_NORMAL, CATEGORY_PHYSICAL, 85, 100, 15, 30, STATUS_PARALYSIS, {0}},

    // Fire attacks
    {10, "Ember", TYPE_FIRE, CATEGORY_SPECIAL, 40, 100, 25, 10, STATUS_BURN, {0}},
    {11, "Flamethrower", TYPE_FIRE, CATEGORY_SPECIAL, 90, 100, 15, 10, STATUS_BURN, {0}},
    {12, "Fire Blast", TYPE_FIRE, CATEGORY_SPECIAL, 110, 85, 5, 10, STATUS_BURN, {0}},

    // Water attacks
    {20, "Water Gun", TYPE_WATER, CATEGORY_SPECIAL, 40, 100, 25, 0, STATUS_NONE, {0}},
    {21, "Bubble Beam", TYPE_WATER, CATEGORY_SPECIAL, 65, 100, 20, 10, STATUS_NONE, {0, 0, 0, 0, 0, -1}},
    {22, "Surf", TYPE_WATER, CATEGORY_SPECIAL, 90, 100, 15, 0, STATUS_NONE, {0}},
    {23, "Hydro Pump", TYPE_WATER, CATEGORY_SPECIAL, 110, 80, 5, 0, STATUS_NONE, {0}},

    // Grass attacks
    {30, "Vine Whip", TYPE_GRASS, CATEGORY_PHYSICAL, 45, 100, 25, 0, STATUS_NONE, {0}},
    {31, "Razor Leaf", TYPE_GRASS, CATEGORY_PHYSICAL, 55, 95, 25, 0, STATUS_NONE, {0}},
    {32, "Solar Beam", TYPE_GRASS, CATEGORY_SPECIAL, 120, 100, 10, 0, STATUS_NONE, {0}},

    // Electric attacks
    {40, "Thunder Shock", TYPE_ELECTRIC, CATEGORY_SPECIAL, 40, 100, 30, 10, STATUS_PARALYSIS, {0}},
    {41, "Thunderbolt", TYPE_ELECTRIC, CATEGORY_SPECIAL, 90, 100, 15, 10, STATUS_PARALYSIS, {0}},
    {42, "Thunder", TYPE_ELECTRIC, CATEGORY_SPECIAL, 110, 70, 10, 30, STATUS_PARALYSIS, {0}},

    // Ice attacks
    {50, "Ice Shard", TYPE_ICE, CATEGORY_PHYSICAL, 40, 100, 30, 0, STATUS_NONE, {0}},
    {51, "Ice Beam", TYPE_ICE, CATEGORY_SPECIAL, 90, 100, 10, 10, STATUS_FREEZE, {0}},
    {52, "Blizzard", TYPE_ICE, CATEGORY_SPECIAL, 110, 70, 5, 10, STATUS_FREEZE, {0}},

    // Fighting attacks
    {60, "Low Kick", TYPE_FIGHTING, CATEGORY_PHYSICAL, 50, 100, 20, 0, STATUS_NONE, {0}},
    {61, "Karate Chop", TYPE_FIGHTING, CATEGORY_PHYSICAL, 50, 100, 25, 0, STATUS_NONE, {0}},

    // Poison attacks
    {70, "Poison Sting", TYPE_POISON, CATEGORY_PHYSICAL, 15, 100, 35, 30, STATUS_POISON, {0}},
    {71, "Sludge", TYPE_POISON, CATEGORY_SPECIAL, 65, 100, 20, 30, STATUS_POISON, {0}},

    // Ground attacks
    {80, "Mud Slap", TYPE_GROUND, CATEGORY_SPECIAL, 20, 100, 10, 100, STATUS_NONE, {0, 0, 0, 0, 0, -1}},
    {81, "Earthquake", TYPE_GROUND, CATEGORY_PHYSICAL, 100, 100, 10, 0, STATUS_NONE, {0}},

    // Flying attacks
    {90, "Gust", TYPE_FLYING, CATEGORY_SPECIAL, 40, 100, 35, 0, STATUS_NONE, {0}},
    {91, "Wing Attack", TYPE_FLYING, CATEGORY_PHYSICAL, 60, 100, 35, 0, STATUS_NONE, {0}},

    // Psychic attacks
    {100, "Confusion", TYPE_PSYCHIC, CATEGORY_SPECIAL, 50, 100, 25, 0, STATUS_NONE, {0}},
    {101, "Psychic", TYPE_PSYCHIC, CATEGORY_SPECIAL, 90, 100, 10, 10, STATUS_NONE, {0, 0, 0, -1, -1, 0}},
};

#define ATTACK_COUNT (sizeof(ATTACK_DATABASE) / sizeof(Attack))

// ====================================================================================
// TYPE SYSTEM
// ====================================================================================

uint8_t get_type_effectiveness(TribomonType attack_type, TribomonType def1, TribomonType def2) {
    if (attack_type >= TYPE_COUNT || def1 >= TYPE_COUNT) {
        return EFFECTIVENESS_NORMAL;
    }

    uint8_t eff1 = TYPE_CHART[attack_type][def1];
    uint8_t eff2 = (def2 != TYPE_NORMAL && def2 < TYPE_COUNT) ?
                   TYPE_CHART[attack_type][def2] : EFFECTIVENESS_NORMAL;

    // Multiply effectiveness (divide by 10 since we stored as integers)
    return (eff1 * eff2) / 10;
}

const char* get_type_name(TribomonType type) {
    static const char *type_names[] = {
        "Normal", "Fire", "Water", "Grass", "Electric", "Ice",
        "Fighting", "Poison", "Ground", "Flying", "Psychic", "Bug",
        "Rock", "Ghost", "Dragon", "Dark", "Steel", "Fairy"
    };
    return (type < TYPE_COUNT) ? type_names[type] : "Unknown";
}

const char* get_status_name(StatusCondition status) {
    static const char *status_names[] = {
        "None", "Burn", "Freeze", "Paralysis", "Poison", "Sleep", "Badly Poisoned"
    };
    return (status < STATUS_COUNT) ? status_names[status] : "Unknown";
}

// ====================================================================================
// SPECIES & ATTACK DATA
// ====================================================================================

const TribomonSpecies* get_species_data(uint16_t species_id) {
    for (size_t i = 0; i < SPECIES_COUNT; i++) {
        if (SPECIES_DATABASE[i].id == species_id) {
            return &SPECIES_DATABASE[i];
        }
    }
    return NULL;
}

const Attack* get_attack_data(uint16_t attack_id) {
    for (size_t i = 0; i < ATTACK_COUNT; i++) {
        if (ATTACK_DATABASE[i].id == attack_id) {
            return &ATTACK_DATABASE[i];
        }
    }
    return NULL;
}

// ====================================================================================
// STAT CALCULATIONS
// ====================================================================================

uint16_t calculate_hp_stat(const TribomonSpecies *species, uint8_t level, uint8_t iv, uint16_t ev) {
    // HP Formula: ((2 * Base + IV + (EV / 4)) * Level / 100) + Level + 10
    uint32_t hp = ((2 * species->base_stats.hp + iv + (ev / 4)) * level) / 100 + level + 10;
    return (hp > MAX_HP_STAT) ? MAX_HP_STAT : (uint16_t)hp;
}

uint16_t calculate_stat(uint16_t base, uint8_t level, uint8_t iv, uint16_t ev) {
    // Stat Formula: ((2 * Base + IV + (EV / 4)) * Level / 100) + 5
    uint32_t stat = ((2 * base + iv + (ev / 4)) * level) / 100 + 5;
    return (stat > MAX_STAT) ? MAX_STAT : (uint16_t)stat;
}

uint32_t calculate_exp_for_level(uint8_t level, GrowthRate growth_rate) {
    uint32_t n = level;
    uint32_t exp = 0;

    switch (growth_rate) {
        case GROWTH_FAST:
            exp = (4 * n * n * n) / 5;
            break;
        case GROWTH_MEDIUM_FAST:
            exp = n * n * n;
            break;
        case GROWTH_MEDIUM_SLOW:
            exp = (6 * n * n * n) / 5 - 15 * n * n + 100 * n - 140;
            break;
        case GROWTH_SLOW:
            exp = (5 * n * n * n) / 4;
            break;
        default:
            exp = n * n * n;
            break;
    }

    return (exp < 0) ? 0 : exp;
}

// ====================================================================================
// TRIBOMON OPERATIONS
// ====================================================================================

void tribomon_init(Tribomon *mon, uint16_t species_id, uint8_t level) {
    if (!mon) return;

    const TribomonSpecies *species = get_species_data(species_id);
    if (!species) {
        ESP_LOGE(TAG, "Invalid species ID: %d", species_id);
        return;
    }

    memset(mon, 0, sizeof(Tribomon));

    mon->species_id = species_id;
    strncpy(mon->nickname, species->name, MAX_TRIBOMON_NAME_LEN - 1);
    mon->level = (level < 1) ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
    mon->exp = calculate_exp_for_level(mon->level, species->growth_rate);
    mon->exp_to_next_level = calculate_exp_for_level(mon->level + 1, species->growth_rate);

    // Generate random IVs (0-31)
    mon->iv_hp = game_random(32);
    mon->iv_attack = game_random(32);
    mon->iv_defense = game_random(32);
    mon->iv_sp_attack = game_random(32);
    mon->iv_sp_defense = game_random(32);
    mon->iv_speed = game_random(32);

    // EVs start at 0
    mon->ev_hp = 0;
    mon->ev_attack = 0;
    mon->ev_defense = 0;
    mon->ev_sp_attack = 0;
    mon->ev_sp_defense = 0;
    mon->ev_speed = 0;

    // Calculate stats
    tribomon_recalculate_stats(mon);
    mon->current_hp = mon->stats.hp;

    // Learn default attacks based on level
    mon->attack_count = 0;
    // TODO: Implement learnset system
    // For now, give some basic attacks
    if (species->type1 == TYPE_FIRE) {
        mon->attacks[0] = *get_attack_data(0);  // Tackle
        mon->attacks[1] = *get_attack_data(10); // Ember
        mon->attack_count = 2;
    } else if (species->type1 == TYPE_WATER) {
        mon->attacks[0] = *get_attack_data(0);  // Tackle
        mon->attacks[1] = *get_attack_data(20); // Water Gun
        mon->attack_count = 2;
    } else if (species->type1 == TYPE_GRASS) {
        mon->attacks[0] = *get_attack_data(0);  // Tackle
        mon->attacks[1] = *get_attack_data(30); // Vine Whip
        mon->attack_count = 2;
    } else {
        mon->attacks[0] = *get_attack_data(0);  // Tackle
        mon->attack_count = 1;
    }

    // Reset PP
    for (int i = 0; i < mon->attack_count; i++) {
        mon->pp_current[i] = mon->attacks[i].pp;
    }

    // Random personality
    mon->personality = esp_random();
    mon->is_shiny = (game_random(4096) == 0); // 1/4096 chance

    mon->status = STATUS_NONE;
    mon->fainted = false;

    ESP_LOGI(TAG, "Initialized %s Lv%d (HP: %d)", mon->nickname, mon->level, mon->stats.hp);
}

void tribomon_recalculate_stats(Tribomon *mon) {
    if (!mon) return;

    const TribomonSpecies *species = get_species_data(mon->species_id);
    if (!species) return;

    mon->stats.hp = calculate_hp_stat(species, mon->level, mon->iv_hp, mon->ev_hp);
    mon->stats.attack = calculate_stat(species->base_stats.attack, mon->level, mon->iv_attack, mon->ev_attack);
    mon->stats.defense = calculate_stat(species->base_stats.defense, mon->level, mon->iv_defense, mon->ev_defense);
    mon->stats.sp_attack = calculate_stat(species->base_stats.sp_attack, mon->level, mon->iv_sp_attack, mon->ev_sp_attack);
    mon->stats.sp_defense = calculate_stat(species->base_stats.sp_defense, mon->level, mon->iv_sp_defense, mon->ev_sp_defense);
    mon->stats.speed = calculate_stat(species->base_stats.speed, mon->level, mon->iv_speed, mon->ev_speed);
}

void tribomon_gain_exp(Tribomon *mon, uint32_t exp) {
    if (!mon || mon->level >= MAX_LEVEL) return;

    mon->exp += exp;
    ESP_LOGI(TAG, "%s gained %lu EXP (%lu/%lu)", mon->nickname, exp, mon->exp, mon->exp_to_next_level);

    while (mon->exp >= mon->exp_to_next_level && mon->level < MAX_LEVEL) {
        tribomon_level_up(mon);
    }
}

bool tribomon_level_up(Tribomon *mon) {
    if (!mon || mon->level >= MAX_LEVEL) return false;

    const TribomonSpecies *species = get_species_data(mon->species_id);
    if (!species) return false;

    uint16_t old_hp = mon->stats.hp;
    mon->level++;
    mon->exp_to_next_level = calculate_exp_for_level(mon->level + 1, species->growth_rate);

    tribomon_recalculate_stats(mon);

    // Heal by HP increase
    uint16_t hp_gain = mon->stats.hp - old_hp;
    mon->current_hp += hp_gain;

    ESP_LOGI(TAG, "%s grew to Lv%d! (+%d HP)", mon->nickname, mon->level, hp_gain);

    // Check evolution
    if (species->evolves_to > 0 && mon->level >= species->evolve_level) {
        ESP_LOGI(TAG, "%s is ready to evolve!", mon->nickname);
        // TODO: Trigger evolution UI
    }

    return true;
}

void tribomon_heal_full(Tribomon *mon) {
    if (!mon) return;

    mon->current_hp = mon->stats.hp;
    mon->status = STATUS_NONE;
    mon->status_turns = 0;
    mon->fainted = false;

    for (int i = 0; i < mon->attack_count; i++) {
        mon->pp_current[i] = mon->attacks[i].pp;
    }

    for (int i = 0; i < 6; i++) {
        mon->stat_stages[i] = 0;
    }
}

void tribomon_take_damage(Tribomon *mon, uint16_t damage) {
    if (!mon || mon->fainted) return;

    if (damage >= mon->current_hp) {
        mon->current_hp = 0;
        mon->fainted = true;
        ESP_LOGI(TAG, "%s fainted!", mon->nickname);
    } else {
        mon->current_hp -= damage;
    }
}

// ====================================================================================
// GAME ENGINE
// ====================================================================================

Game* game_engine_init(void) {
    if (g_initialized) {
        return &g_game;
    }

    memset(&g_game, 0, sizeof(Game));
    g_game.current_state = GAME_STATE_MENU;
    g_game.random_seed = esp_random();
    game_seed_random(g_game.random_seed);

    g_initialized = true;
    ESP_LOGI(TAG, "Game engine initialized");

    return &g_game;
}

Game* game_engine_get(void) {
    if (!g_initialized) {
        return game_engine_init();
    }
    return &g_game;
}

void game_new(const char *player_name, uint16_t starter_id) {
    memset(&g_game.player, 0, sizeof(Player));

    strncpy(g_game.player.name, player_name, sizeof(g_game.player.name) - 1);
    g_game.player.trainer_id = game_random(65536);
    g_game.player.money = 3000; // Starting money
    g_game.player.badges = 0;

    // Add starter Tribomon
    Tribomon starter;
    tribomon_init(&starter, starter_id, 5);
    party_add_tribomon(&starter);

    // Starting items
    inventory_add_item(ITEM_POKEBALL, 5);
    inventory_add_item(ITEM_POTION, 3);

    g_game.intro_complete = false;
    g_game.tutorial_complete = false;
    g_game.current_state = GAME_STATE_OVERWORLD;

    ESP_LOGI(TAG, "New game started for %s with starter #%d", player_name, starter_id);
}

void game_update(void) {
    g_game.player.play_time_seconds++;
}

// ====================================================================================
// PARTY MANAGEMENT
// ====================================================================================

bool party_add_tribomon(const Tribomon *mon) {
    if (!mon || g_game.player.party_count >= MAX_PARTY_SIZE) {
        return false;
    }

    memcpy(&g_game.player.party[g_game.player.party_count], mon, sizeof(Tribomon));
    g_game.player.party_count++;

    ESP_LOGI(TAG, "Added %s to party (slot %d)", mon->nickname, g_game.player.party_count - 1);
    return true;
}

bool party_remove_tribomon(uint8_t slot) {
    if (slot >= g_game.player.party_count) {
        return false;
    }

    // Shift remaining Tribomon
    for (uint8_t i = slot; i < g_game.player.party_count - 1; i++) {
        memcpy(&g_game.player.party[i], &g_game.player.party[i + 1], sizeof(Tribomon));
    }

    g_game.player.party_count--;
    return true;
}

void party_swap(uint8_t slot1, uint8_t slot2) {
    if (slot1 >= g_game.player.party_count || slot2 >= g_game.player.party_count) {
        return;
    }

    Tribomon temp = g_game.player.party[slot1];
    g_game.player.party[slot1] = g_game.player.party[slot2];
    g_game.player.party[slot2] = temp;
}

Tribomon* party_get_tribomon(uint8_t slot) {
    if (slot >= g_game.player.party_count) {
        return NULL;
    }
    return &g_game.player.party[slot];
}

Tribomon* party_get_first_alive(void) {
    for (uint8_t i = 0; i < g_game.player.party_count; i++) {
        if (!g_game.player.party[i].fainted) {
            return &g_game.player.party[i];
        }
    }
    return NULL;
}

bool party_all_fainted(void) {
    return party_get_first_alive() == NULL;
}

void party_heal_all(void) {
    for (uint8_t i = 0; i < g_game.player.party_count; i++) {
        tribomon_heal_full(&g_game.player.party[i]);
    }
    ESP_LOGI(TAG, "Party healed!");
}

// ====================================================================================
// INVENTORY
// ====================================================================================

bool inventory_add_item(ItemType item_type, uint16_t quantity) {
    // Check if item already in inventory
    for (uint8_t i = 0; i < g_game.player.inventory_count; i++) {
        if (g_game.player.inventory[i].type == item_type) {
            g_game.player.inventory[i].quantity += quantity;
            return true;
        }
    }

    // Add new slot
    if (g_game.player.inventory_count >= MAX_INVENTORY_SLOTS) {
        return false;
    }

    g_game.player.inventory[g_game.player.inventory_count].type = item_type;
    g_game.player.inventory[g_game.player.inventory_count].quantity = quantity;
    g_game.player.inventory_count++;

    return true;
}

bool inventory_remove_item(ItemType item_type, uint16_t quantity) {
    for (uint8_t i = 0; i < g_game.player.inventory_count; i++) {
        if (g_game.player.inventory[i].type == item_type) {
            if (g_game.player.inventory[i].quantity < quantity) {
                return false;
            }
            g_game.player.inventory[i].quantity -= quantity;
            return true;
        }
    }
    return false;
}

uint16_t inventory_get_quantity(ItemType item_type) {
    for (uint8_t i = 0; i < g_game.player.inventory_count; i++) {
        if (g_game.player.inventory[i].type == item_type) {
            return g_game.player.inventory[i].quantity;
        }
    }
    return 0;
}

const char* inventory_get_item_name(ItemType item_type) {
    static const char *names[] = {
        "Poke Ball", "Great Ball", "Ultra Ball", "Master Ball",
        "Potion", "Super Potion", "Hyper Potion", "Max Potion",
        "Revive", "Max Revive", "Antidote", "Burn Heal",
        "Ice Heal", "Paralyz Heal", "Awakening", "Full Heal",
        "Rare Candy", "Escape Rope", "Repel"
    };
    return (item_type < ITEM_COUNT) ? names[item_type] : "Unknown";
}

// ====================================================================================
// RANDOM
// ====================================================================================

void game_seed_random(uint32_t seed) {
    g_game.random_seed = seed;
}

uint32_t game_random(uint32_t max) {
    if (max == 0) return 0;
    g_game.random_seed = (g_game.random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return g_game.random_seed % max;
}

uint32_t game_random_range(uint32_t min, uint32_t max) {
    if (min >= max) return min;
    return min + game_random(max - min + 1);
}

uint32_t random_uint32(void) {
    return esp_random();
}

// ====================================================================================
// MONEY
// ====================================================================================

void player_add_money(uint32_t amount) {
    g_game.player.money += amount;
    ESP_LOGI(TAG, "Gained $%lu (total: $%lu)", amount, g_game.player.money);
}

bool player_remove_money(uint32_t amount) {
    if (g_game.player.money < amount) {
        return false;
    }
    g_game.player.money -= amount;
    return true;
}

uint32_t player_get_money(void) {
    return g_game.player.money;
}

// ====================================================================================
// SAVE/LOAD (NVS)
// ====================================================================================

bool game_save(uint8_t slot) {
    if (slot > 2) return false;

    nvs_handle_t nvs_handle;
    esp_err_t err;

    char key[16];
    snprintf(key, sizeof(key), "game_slot_%d", slot);

    err = nvs_open("tribomon", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Calculate checksum (simple sum for now)
    uint32_t checksum = 0;
    uint8_t *data = (uint8_t*)&g_game;
    for (size_t i = 0; i < sizeof(Game) - sizeof(uint32_t); i++) {
        checksum += data[i];
    }
    g_game.checksum = checksum;

    err = nvs_set_blob(nvs_handle, key, &g_game, sizeof(Game));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save game: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Game saved to slot %d", slot);
        return true;
    }

    return false;
}

bool game_load(uint8_t slot) {
    if (slot > 2) return false;

    nvs_handle_t nvs_handle;
    esp_err_t err;

    char key[16];
    snprintf(key, sizeof(key), "game_slot_%d", slot);

    err = nvs_open("tribomon", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = sizeof(Game);
    err = nvs_get_blob(nvs_handle, key, &g_game, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK || required_size != sizeof(Game)) {
        ESP_LOGE(TAG, "Failed to load game: %s", esp_err_to_name(err));
        return false;
    }

    // Verify checksum
    uint32_t checksum = 0;
    uint8_t *data = (uint8_t*)&g_game;
    for (size_t i = 0; i < sizeof(Game) - sizeof(uint32_t); i++) {
        checksum += data[i];
    }

    if (checksum != g_game.checksum) {
        ESP_LOGE(TAG, "Save data corrupted!");
        return false;
    }

    ESP_LOGI(TAG, "Game loaded from slot %d", slot);
    return true;
}

bool game_save_exists(uint8_t slot) {
    if (slot > 2) return false;

    nvs_handle_t nvs_handle;
    esp_err_t err;

    char key[16];
    snprintf(key, sizeof(key), "game_slot_%d", slot);

    err = nvs_open("tribomon", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, key, NULL, &required_size);
    nvs_close(nvs_handle);

    return (err == ESP_OK && required_size == sizeof(Game));
}
