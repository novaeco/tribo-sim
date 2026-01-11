/**
 * @file tribomon_types.h
 * @brief Tribomon Game - Data Structures and Type Definitions
 *
 * Defines all core game structures for the Tribomon Pokemon-like game
 * running on ESP32-P4 with LVGL UI.
 *
 * Features:
 *   - 18 elemental types with weakness/resistance table
 *   - 150+ Tribomon species
 *   - Turn-based battle system
 *   - Party management (6 Tribomon max)
 *   - Inventory system
 *   - Capture mechanics
 */

#ifndef TRIBOMON_TYPES_H
#define TRIBOMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// ====================================================================================
// CONSTANTS
// ====================================================================================

#define MAX_TRIBOMON_NAME_LEN   16
#define MAX_ATTACK_NAME_LEN     16
#define MAX_PARTY_SIZE          6
#define MAX_ATTACKS_PER_MON     4
#define MAX_INVENTORY_SLOTS     20
#define MAX_LEVEL               100

#define BASE_CAPTURE_RATE       45   // Base probability (0-255)
#define MAX_HP_STAT             255
#define MAX_STAT                255

// ====================================================================================
// ELEMENTAL TYPES
// ====================================================================================

typedef enum {
    TYPE_NORMAL = 0,
    TYPE_FIRE,
    TYPE_WATER,
    TYPE_GRASS,
    TYPE_ELECTRIC,
    TYPE_ICE,
    TYPE_FIGHTING,
    TYPE_POISON,
    TYPE_GROUND,
    TYPE_FLYING,
    TYPE_PSYCHIC,
    TYPE_BUG,
    TYPE_ROCK,
    TYPE_GHOST,
    TYPE_DRAGON,
    TYPE_DARK,
    TYPE_STEEL,
    TYPE_FAIRY,
    TYPE_COUNT  // Total number of types
} TribomonType;

// Type effectiveness multipliers (stored as uint8_t, divide by 10 for actual value)
// 20 = 2.0x (super effective), 10 = 1.0x (normal), 5 = 0.5x (not very effective), 0 = immune
typedef enum {
    EFFECTIVENESS_IMMUNE = 0,
    EFFECTIVENESS_QUARTER = 2,   // 0.25x
    EFFECTIVENESS_HALF = 5,       // 0.5x
    EFFECTIVENESS_NORMAL = 10,    // 1.0x
    EFFECTIVENESS_DOUBLE = 20,    // 2.0x
    EFFECTIVENESS_QUAD = 40       // 4.0x
} TypeEffectiveness;

// ====================================================================================
// STATS & GROWTH
// ====================================================================================

typedef struct {
    uint16_t hp;
    uint16_t attack;
    uint16_t defense;
    uint16_t sp_attack;
    uint16_t sp_defense;
    uint16_t speed;
} Stats;

typedef enum {
    GROWTH_ERRATIC = 0,
    GROWTH_FAST,
    GROWTH_MEDIUM_FAST,
    GROWTH_MEDIUM_SLOW,
    GROWTH_SLOW,
    GROWTH_FLUCTUATING
} GrowthRate;

// ====================================================================================
// STATUS CONDITIONS
// ====================================================================================

typedef enum {
    STATUS_NONE = 0,
    STATUS_BURN,        // Loses 1/16 HP each turn, Attack halved
    STATUS_FREEZE,      // Cannot move, 20% chance to thaw each turn
    STATUS_PARALYSIS,   // Speed quartered, 25% chance to not move
    STATUS_POISON,      // Loses 1/8 HP each turn
    STATUS_SLEEP,       // Cannot move for 1-3 turns
    STATUS_BADLY_POISONED, // Loses n/16 HP (n = turns poisoned)
    STATUS_COUNT
} StatusCondition;

// ====================================================================================
// ATTACK DEFINITIONS
// ====================================================================================

typedef enum {
    CATEGORY_PHYSICAL = 0,  // Uses Attack vs Defense
    CATEGORY_SPECIAL,       // Uses Sp.Attack vs Sp.Defense
    CATEGORY_STATUS         // Non-damaging (stat changes, status effects)
} AttackCategory;

typedef struct {
    uint16_t id;
    char name[MAX_ATTACK_NAME_LEN];
    TribomonType type;
    AttackCategory category;
    uint8_t power;          // 0-255 (0 for status moves)
    uint8_t accuracy;       // 0-100 (percentage)
    uint8_t pp;             // Power Points (max uses)
    uint8_t effect_chance;  // 0-100 (% chance for secondary effect)
    StatusCondition status; // Status inflicted on target (if any)
    int8_t stat_change[6];  // Stat stage changes: HP, Atk, Def, SpA, SpD, Spe (-6 to +6)
} Attack;

// ====================================================================================
// TRIBOMON SPECIES
// ====================================================================================

typedef struct {
    uint16_t id;                    // National Dex number
    char name[MAX_TRIBOMON_NAME_LEN];
    TribomonType type1;
    TribomonType type2;             // TYPE_NORMAL if single type
    Stats base_stats;
    uint8_t capture_rate;           // 0-255 (higher = easier to catch)
    GrowthRate growth_rate;
    uint16_t base_exp_yield;        // EXP given when defeated
    uint16_t evolves_to;            // Species ID, 0 if doesn't evolve
    uint8_t evolve_level;           // Level required, 0 if no evolution
} TribomonSpecies;

// ====================================================================================
// TRIBOMON INSTANCE (Party/Wild)
// ====================================================================================

typedef struct {
    uint16_t species_id;            // Reference to TribomonSpecies
    char nickname[MAX_TRIBOMON_NAME_LEN];
    uint8_t level;                  // 1-100
    uint32_t exp;                   // Current experience points
    uint32_t exp_to_next_level;     // EXP needed for next level

    // Current battle stats
    Stats stats;                    // Actual stats (base + IVs + EVs + level)
    uint16_t current_hp;

    // Individual Values (IVs) - 0-31 for each stat (genetic)
    uint8_t iv_hp;
    uint8_t iv_attack;
    uint8_t iv_defense;
    uint8_t iv_sp_attack;
    uint8_t iv_sp_defense;
    uint8_t iv_speed;

    // Effort Values (EVs) - 0-255 for each stat, max 510 total
    uint16_t ev_hp;
    uint16_t ev_attack;
    uint16_t ev_defense;
    uint16_t ev_sp_attack;
    uint16_t ev_sp_defense;
    uint16_t ev_speed;

    // Learned attacks
    Attack attacks[MAX_ATTACKS_PER_MON];
    uint8_t attack_count;           // Number of learned attacks (0-4)
    uint8_t pp_current[MAX_ATTACKS_PER_MON]; // Current PP for each attack

    // Status
    StatusCondition status;
    uint8_t status_turns;           // Turns remaining for sleep/freeze, or turns poisoned

    // Battle state
    int8_t stat_stages[6];          // -6 to +6 for each stat (reset after battle)
    bool fainted;

    // Metadata
    uint32_t personality;           // Random value for nature, gender, etc.
    bool is_shiny;                  // 1/4096 chance
    uint16_t original_trainer_id;   // Player who caught it
} Tribomon;

// ====================================================================================
// PLAYER DATA
// ====================================================================================

typedef enum {
    ITEM_POKEBALL = 0,
    ITEM_GREATBALL,
    ITEM_ULTRABALL,
    ITEM_MASTERBALL,
    ITEM_POTION,
    ITEM_SUPER_POTION,
    ITEM_HYPER_POTION,
    ITEM_MAX_POTION,
    ITEM_REVIVE,
    ITEM_MAX_REVIVE,
    ITEM_ANTIDOTE,
    ITEM_BURN_HEAL,
    ITEM_ICE_HEAL,
    ITEM_PARALYZ_HEAL,
    ITEM_AWAKENING,
    ITEM_FULL_HEAL,
    ITEM_RARE_CANDY,
    ITEM_ESCAPE_ROPE,
    ITEM_REPEL,
    ITEM_COUNT
} ItemType;

typedef struct {
    ItemType type;
    uint16_t quantity;
} InventorySlot;

typedef struct {
    char name[16];
    uint16_t trainer_id;
    uint32_t money;
    uint8_t badges;                 // Bitfield of collected badges (0-8)

    Tribomon party[MAX_PARTY_SIZE];
    uint8_t party_count;            // Number of Tribomon in party (0-6)

    InventorySlot inventory[MAX_INVENTORY_SLOTS];
    uint8_t inventory_count;

    // Progress
    uint16_t pokedex_seen;          // Number of species seen
    uint16_t pokedex_caught;        // Number of species caught
    uint32_t play_time_seconds;

    // Current location
    uint16_t map_x;
    uint16_t map_y;
} Player;

// ====================================================================================
// BATTLE STATE
// ====================================================================================

typedef enum {
    BATTLE_TYPE_WILD = 0,
    BATTLE_TYPE_TRAINER,
    BATTLE_TYPE_MULTIPLAYER
} BattleType;

typedef enum {
    BATTLE_ACTION_ATTACK = 0,
    BATTLE_ACTION_ITEM,
    BATTLE_ACTION_SWITCH,
    BATTLE_ACTION_RUN
} BattleAction;

typedef struct {
    BattleAction action;
    union {
        uint8_t attack_index;       // 0-3 for ATTACK
        struct {
            ItemType item;
            uint8_t target_slot;    // Party slot for items
        } item_use;
        uint8_t switch_to_slot;     // 0-5 for SWITCH
    } data;
} BattleTurn;

typedef struct {
    BattleType type;
    bool active;
    bool player_turn;               // true if player's turn

    // Player side
    Tribomon *player_active;        // Current Tribomon in battle
    uint8_t player_active_slot;     // Index in party

    // Enemy side
    Tribomon enemy_active;          // Wild Tribomon or trainer's Tribomon
    uint16_t enemy_trainer_id;      // 0 for wild battles

    // Battle state
    uint16_t turn_count;
    bool can_run;                   // false for trainer battles
    bool can_catch;                 // false for trainer battles

    // Turn queue
    BattleTurn player_turn_data;
    BattleTurn enemy_turn_data;

    // Weather/field effects
    uint8_t weather;                // 0=none, 1=rain, 2=sun, 3=sandstorm, 4=hail
    uint8_t weather_turns;

    // Results
    bool player_won;
    uint32_t exp_gained;
    uint32_t money_gained;
} BattleState;

// ====================================================================================
// GAME STATE
// ====================================================================================

typedef enum {
    GAME_STATE_MENU = 0,
    GAME_STATE_OVERWORLD,
    GAME_STATE_BATTLE,
    GAME_STATE_INVENTORY,
    GAME_STATE_PARTY,
    GAME_STATE_POKEDEX,
    GAME_STATE_SETTINGS
} GameState;

typedef struct {
    GameState current_state;
    Player player;
    BattleState battle;

    // Random encounter
    uint32_t steps_since_encounter;
    uint32_t random_seed;

    // Game flags
    bool intro_complete;
    bool tutorial_complete;

    // Save data
    uint8_t save_slot;              // 0-2 (3 save slots)
    uint32_t checksum;              // For save validation
} Game;

// ====================================================================================
// FUNCTION DECLARATIONS (implemented in game_engine.c)
// ====================================================================================

// Type effectiveness
uint8_t get_type_effectiveness(TribomonType attack_type, TribomonType defender_type1, TribomonType defender_type2);

// Species data
const TribomonSpecies* get_species_data(uint16_t species_id);
const Attack* get_attack_data(uint16_t attack_id);

// Stat calculations
uint16_t calculate_hp_stat(const TribomonSpecies *species, uint8_t level, uint8_t iv, uint16_t ev);
uint16_t calculate_stat(uint16_t base, uint8_t level, uint8_t iv, uint16_t ev);
uint32_t calculate_exp_for_level(uint8_t level, GrowthRate growth_rate);

// Tribomon operations
void tribomon_init(Tribomon *mon, uint16_t species_id, uint8_t level);
void tribomon_gain_exp(Tribomon *mon, uint32_t exp);
bool tribomon_level_up(Tribomon *mon);
void tribomon_recalculate_stats(Tribomon *mon);
void tribomon_heal_full(Tribomon *mon);
void tribomon_take_damage(Tribomon *mon, uint16_t damage);

// Battle mechanics
uint16_t calculate_damage(const Tribomon *attacker, const Tribomon *defender,
                          const Attack *attack, bool *is_critical);
bool attempt_capture(const Tribomon *target, ItemType ball_type);
void apply_status_effect(Tribomon *target, StatusCondition status);
bool can_attack(const Tribomon *mon); // Check if status prevents attack

// Utility
uint32_t random_uint32(void);
const char* get_type_name(TribomonType type);
const char* get_status_name(StatusCondition status);

#endif // TRIBOMON_TYPES_H
