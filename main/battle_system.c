/**
 * @file battle_system.c
 * @brief Tribomon Battle System Implementation
 */

#include "battle_system.h"
#include "game_engine.h"
#include "tribomon_types.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "BATTLE";

// Battle message buffer
static char battle_message[128] = {0};

// Forward declaration
static bool execute_single_action(bool is_player);

// ====================================================================================
// BATTLE MANAGEMENT
// ====================================================================================

BattleState* battle_start_wild(Tribomon *wild_tribomon) {
    if (!wild_tribomon) return NULL;

    Game *game = game_engine_get();
    if (!game) return NULL;

    memset(&game->battle, 0, sizeof(BattleState));

    game->battle.type = BATTLE_TYPE_WILD;
    game->battle.active = true;
    game->battle.can_run = true;
    game->battle.can_catch = true;

    // Set player's first alive Tribomon
    game->battle.player_active = party_get_first_alive();
    if (!game->battle.player_active) {
        ESP_LOGE(TAG, "No usable Tribomon!");
        return NULL;
    }

    // Find active slot
    for (uint8_t i = 0; i < game->player.party_count; i++) {
        if (&game->player.party[i] == game->battle.player_active) {
            game->battle.player_active_slot = i;
            break;
        }
    }

    // Copy wild Tribomon
    memcpy(&game->battle.enemy_active, wild_tribomon, sizeof(Tribomon));
    game->battle.enemy_trainer_id = 0;

    game->battle.turn_count = 0;
    game->battle.player_won = false;

    game->current_state = GAME_STATE_BATTLE;

    const TribomonSpecies *species = get_species_data(wild_tribomon->species_id);
    snprintf(battle_message, sizeof(battle_message),
             "A wild %s appeared!", species ? species->name : "Tribomon");

    ESP_LOGI(TAG, "Wild battle started: %s Lv%d vs %s Lv%d",
             game->battle.player_active->nickname, game->battle.player_active->level,
             game->battle.enemy_active.nickname, game->battle.enemy_active.level);

    return &game->battle;
}

BattleState* battle_start_trainer(Tribomon *trainer_tribomon, uint16_t trainer_id) {
    if (!trainer_tribomon) return NULL;

    BattleState *battle = battle_start_wild(trainer_tribomon);
    if (!battle) return NULL;

    battle->type = BATTLE_TYPE_TRAINER;
    battle->can_run = false;
    battle->can_catch = false;
    battle->enemy_trainer_id = trainer_id;

    snprintf(battle_message, sizeof(battle_message),
             "Trainer sends out %s!", trainer_tribomon->nickname);

    return battle;
}

BattleState* battle_get_state(void) {
    Game *game = game_engine_get();
    return (game && game->battle.active) ? &game->battle : NULL;
}

void battle_end(bool player_won) {
    Game *game = game_engine_get();
    if (!game) return;

    game->battle.player_won = player_won;
    game->battle.active = false;
    game->current_state = GAME_STATE_OVERWORLD;

    if (player_won) {
        ESP_LOGI(TAG, "Player won! Gained %lu EXP and $%lu",
                 game->battle.exp_gained, game->battle.money_gained);

        distribute_experience(game->battle.exp_gained);

        if (game->battle.money_gained > 0) {
            player_add_money(game->battle.money_gained);
        }

        snprintf(battle_message, sizeof(battle_message), "You won the battle!");
    } else {
        ESP_LOGI(TAG, "Player lost!");
        snprintf(battle_message, sizeof(battle_message), "You blacked out...");

        // Lose half money
        uint32_t money_lost = player_get_money() / 2;
        player_remove_money(money_lost);
    }
}

bool battle_is_active(void) {
    BattleState *battle = battle_get_state();
    return (battle != NULL);
}

// ====================================================================================
// PLAYER ACTIONS
// ====================================================================================

bool battle_player_attack(uint8_t attack_index) {
    BattleState *battle = battle_get_state();
    if (!battle || attack_index >= MAX_ATTACKS_PER_MON) {
        return false;
    }

    if (attack_index >= battle->player_active->attack_count) {
        ESP_LOGW(TAG, "Invalid attack index");
        return false;
    }

    if (battle->player_active->pp_current[attack_index] == 0) {
        snprintf(battle_message, sizeof(battle_message), "No PP left!");
        return false;
    }

    battle->player_turn_data.action = BATTLE_ACTION_ATTACK;
    battle->player_turn_data.data.attack_index = attack_index;

    ESP_LOGI(TAG, "Player chose attack %d", attack_index);
    return true;
}

bool battle_player_use_item(ItemType item_type, uint8_t target_slot) {
    BattleState *battle = battle_get_state();
    if (!battle) return false;

    if (inventory_get_quantity(item_type) == 0) {
        snprintf(battle_message, sizeof(battle_message), "No items left!");
        return false;
    }

    battle->player_turn_data.action = BATTLE_ACTION_ITEM;
    battle->player_turn_data.data.item_use.item = item_type;
    battle->player_turn_data.data.item_use.target_slot = target_slot;

    ESP_LOGI(TAG, "Player used %s", inventory_get_item_name(item_type));
    return true;
}

bool battle_player_switch(uint8_t party_slot) {
    BattleState *battle = battle_get_state();
    if (!battle) return false;

    Tribomon *target = party_get_tribomon(party_slot);
    if (!target || target->fainted) {
        snprintf(battle_message, sizeof(battle_message), "Can't switch to fainted Tribomon!");
        return false;
    }

    if (party_slot == battle->player_active_slot) {
        snprintf(battle_message, sizeof(battle_message), "Already in battle!");
        return false;
    }

    battle->player_turn_data.action = BATTLE_ACTION_SWITCH;
    battle->player_turn_data.data.switch_to_slot = party_slot;

    ESP_LOGI(TAG, "Player switching to slot %d", party_slot);
    return true;
}

bool battle_player_run(void) {
    BattleState *battle = battle_get_state();
    if (!battle) return false;

    if (!battle->can_run) {
        snprintf(battle_message, sizeof(battle_message), "Can't run from a trainer battle!");
        return false;
    }

    // Calculate escape probability
    // Higher player speed = higher chance
    uint16_t player_speed = apply_stat_modifier(battle->player_active->stats.speed,
                                                 battle->player_active->stat_stages[5]);
    uint16_t enemy_speed = apply_stat_modifier(battle->enemy_active.stats.speed,
                                                battle->enemy_active.stat_stages[5]);

    uint8_t escape_chance = ((player_speed * 128) / enemy_speed) + (30 * battle->turn_count);

    if (game_random(256) < escape_chance) {
        snprintf(battle_message, sizeof(battle_message), "Got away safely!");
        battle_end(false);
        ESP_LOGI(TAG, "Escaped successfully");
        return true;
    } else {
        snprintf(battle_message, sizeof(battle_message), "Can't escape!");
        battle->player_turn_data.action = BATTLE_ACTION_RUN;
        ESP_LOGI(TAG, "Failed to escape");
        return true; // Still consumes turn
    }
}

// ====================================================================================
// TURN EXECUTION
// ====================================================================================

bool battle_execute_turn(void) {
    BattleState *battle = battle_get_state();
    if (!battle) return false;

    battle->turn_count++;

    // Generate enemy action
    battle->enemy_turn_data = battle_ai_generate_action(&battle->enemy_active);

    // Determine turn order (higher speed goes first)
    bool player_first = true;

    // Switching always goes first
    if (battle->player_turn_data.action == BATTLE_ACTION_SWITCH) {
        player_first = true;
    } else if (battle->enemy_turn_data.action == BATTLE_ACTION_SWITCH) {
        player_first = false;
    } else if (battle->player_turn_data.action == BATTLE_ACTION_ATTACK &&
               battle->enemy_turn_data.action == BATTLE_ACTION_ATTACK) {
        // Compare speed (with priority moves later)
        uint16_t player_speed = apply_stat_modifier(battle->player_active->stats.speed,
                                                     battle->player_active->stat_stages[5]);
        uint16_t enemy_speed = apply_stat_modifier(battle->enemy_active.stats.speed,
                                                    battle->enemy_active.stat_stages[5]);
        player_first = (player_speed >= enemy_speed);
    }

    // Execute turns
    if (player_first) {
        if (!execute_single_action(true)) return false;
        if (!execute_single_action(false)) return false;
    } else {
        if (!execute_single_action(false)) return false;
        if (!execute_single_action(true)) return false;
    }

    // End of turn status effects
    process_status_damage(battle->player_active);
    process_status_damage(&battle->enemy_active);

    // Check battle end conditions
    if (battle->player_active->fainted) {
        if (!battle_force_switch()) {
            // All fainted - player loses
            battle_end(false);
            return false;
        }
    }

    if (battle->enemy_active.fainted) {
        // Player wins
        const TribomonSpecies *species = get_species_data(battle->enemy_active.species_id);
        battle->exp_gained = calculate_exp_gain(&battle->enemy_active,
                                                 battle->type == BATTLE_TYPE_WILD);

        if (battle->type == BATTLE_TYPE_TRAINER) {
            battle->money_gained = calculate_money_gain(battle->enemy_trainer_id,
                                                        &battle->enemy_active);
        }

        snprintf(battle_message, sizeof(battle_message),
                 "Enemy %s fainted!", species ? species->name : "Tribomon");

        battle_end(true);
        return false;
    }

    return true;
}

static bool execute_single_action(bool is_player) {
    BattleState *battle = battle_get_state();
    if (!battle) return false;

    Tribomon *actor = is_player ? battle->player_active : &battle->enemy_active;
    Tribomon *target = is_player ? &battle->enemy_active : battle->player_active;
    BattleTurn *turn = is_player ? &battle->player_turn_data : &battle->enemy_turn_data;

    const char *actor_name = actor->nickname;
    const char *target_name = target->nickname;

    // Check if actor can move (status effects)
    if (!can_attack(actor)) {
        return true; // Skip turn but continue battle
    }

    switch (turn->action) {
        case BATTLE_ACTION_ATTACK: {
            uint8_t atk_idx = turn->data.attack_index;
            if (atk_idx >= actor->attack_count) return true;

            Attack *attack = &actor->attacks[atk_idx];

            // Check PP
            if (actor->pp_current[atk_idx] == 0) {
                snprintf(battle_message, sizeof(battle_message),
                         "%s has no PP left!", actor_name);
                return true;
            }

            // Decrease PP
            actor->pp_current[atk_idx]--;

            // Check hit
            if (!check_hit(attack, actor, target)) {
                snprintf(battle_message, sizeof(battle_message),
                         "%s's attack missed!", actor_name);
                return true;
            }

            // Calculate damage
            bool is_critical = false;
            uint16_t damage = calculate_damage(actor, target, attack, &is_critical);

            // Apply damage
            tribomon_take_damage(target, damage);

            // Format message
            uint8_t effectiveness = get_type_effectiveness(attack->type,
                                                          get_species_data(target->species_id)->type1,
                                                          get_species_data(target->species_id)->type2);

            snprintf(battle_message, sizeof(battle_message),
                     "%s used %s! %s",
                     actor_name, attack->name,
                     is_critical ? "Critical hit!" : battle_format_effectiveness(effectiveness));

            ESP_LOGI(TAG, "%s used %s for %d damage%s",
                     actor_name, attack->name, damage,
                     is_critical ? " (CRIT)" : "");

            // Apply status effect
            if (attack->effect_chance > 0 && attack->status != STATUS_NONE) {
                if (game_random(100) < attack->effect_chance) {
                    apply_status_effect(target, attack->status);
                }
            }

            break;
        }

        case BATTLE_ACTION_ITEM: {
            // Handle item use (capture handled separately)
            ItemType item = turn->data.item_use.item;
            uint8_t slot = turn->data.item_use.target_slot;

            if (item >= ITEM_POKEBALL && item <= ITEM_MASTERBALL) {
                // Capture attempt
                if (!battle->can_catch) {
                    snprintf(battle_message, sizeof(battle_message),
                             "Can't catch trainer's Tribomon!");
                    return true;
                }

                if (attempt_capture(target, item)) {
                    // Success!
                    snprintf(battle_message, sizeof(battle_message),
                             "Gotcha! %s was caught!", target_name);

                    if (party_add_tribomon(target)) {
                        ESP_LOGI(TAG, "Captured %s and added to party", target_name);
                    } else {
                        ESP_LOGI(TAG, "Captured %s (party full, sent to PC)", target_name);
                    }

                    // Register in Pokedex
                    pokedex_register_caught(target->species_id);

                    battle_end(true);
                    return false;
                } else {
                    snprintf(battle_message, sizeof(battle_message),
                             "%s broke free!", target_name);
                }

                inventory_remove_item(item, 1);
            } else {
                // Healing/status cure items
                inventory_use_item(item, slot);
                snprintf(battle_message, sizeof(battle_message),
                         "Used %s!", inventory_get_item_name(item));
            }
            break;
        }

        case BATTLE_ACTION_SWITCH: {
            if (is_player) {
                battle->player_active = party_get_tribomon(turn->data.switch_to_slot);
                battle->player_active_slot = turn->data.switch_to_slot;
                snprintf(battle_message, sizeof(battle_message),
                         "Go, %s!", battle->player_active->nickname);
            }
            break;
        }

        case BATTLE_ACTION_RUN:
            // Already handled in battle_player_run()
            break;
    }

    return true;
}

// ====================================================================================
// DAMAGE CALCULATION
// ====================================================================================

uint16_t calculate_damage(const Tribomon *attacker, const Tribomon *defender,
                          const Attack *attack, bool *is_critical) {
    if (!attacker || !defender || !attack) return 0;

    // Status moves don't deal damage
    if (attack->category == CATEGORY_STATUS || attack->power == 0) {
        return 0;
    }

    // Get species data
    const TribomonSpecies *atk_species = get_species_data(attacker->species_id);
    const TribomonSpecies *def_species = get_species_data(defender->species_id);
    if (!atk_species || !def_species) return 0;

    // Check critical hit
    bool crit = check_critical(attacker);
    if (is_critical) *is_critical = crit;

    // Get appropriate attack/defense stats
    uint16_t atk_stat, def_stat;

    if (attack->category == CATEGORY_PHYSICAL) {
        atk_stat = apply_stat_modifier(attacker->stats.attack, attacker->stat_stages[1]);
        def_stat = apply_stat_modifier(defender->stats.defense, defender->stat_stages[2]);
    } else {
        atk_stat = apply_stat_modifier(attacker->stats.sp_attack, attacker->stat_stages[3]);
        def_stat = apply_stat_modifier(defender->stats.sp_defense, defender->stat_stages[4]);
    }

    // Ignore stat drops on critical hit for attacker
    if (crit && attacker->stat_stages[1] < 0) {
        atk_stat = (attack->category == CATEGORY_PHYSICAL) ?
                   attacker->stats.attack : attacker->stats.sp_attack;
    }

    // Base damage formula
    float damage = ((2.0f * attacker->level / 5.0f + 2.0f) * attack->power * atk_stat / def_stat) / 50.0f + 2.0f;

    // Critical hit multiplier
    if (crit) {
        damage *= 1.5f;
    }

    // STAB (Same Type Attack Bonus)
    if (attack->type == atk_species->type1 || attack->type == atk_species->type2) {
        damage *= 1.5f;
    }

    // Type effectiveness
    uint8_t effectiveness = get_type_effectiveness(attack->type, def_species->type1, def_species->type2);
    damage *= (effectiveness / 10.0f);

    // Random factor (85-100%)
    damage *= (game_random_range(85, 100) / 100.0f);

    // Burn halves physical attack damage
    if (attacker->status == STATUS_BURN && attack->category == CATEGORY_PHYSICAL) {
        damage *= 0.5f;
    }

    return (damage < 1.0f) ? 1 : (uint16_t)damage;
}

uint16_t apply_stat_modifier(uint16_t stat, int8_t stage) {
    if (stage == 0) return stat;

    // Stat stages: -6 to +6
    // Multipliers: 2/8, 2/7, 2/6, 2/5, 2/4, 2/3, 3/2, 4/2, 5/2, 6/2, 7/2, 8/2
    static const float multipliers[] = {
        0.25f, 0.2857f, 0.3333f, 0.4f, 0.5f, 0.6667f, 1.0f,  // -6 to 0
        1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f                    // +1 to +6
    };

    int8_t index = stage + 6;
    if (index < 0) index = 0;
    if (index > 12) index = 12;

    return (uint16_t)(stat * multipliers[index]);
}

bool check_hit(const Attack *attack, const Tribomon *attacker, const Tribomon *defender) {
    if (!attack) return false;

    // Moves with 0 accuracy always hit
    if (attack->accuracy == 0) return true;

    // TODO: Implement accuracy/evasion stages
    uint8_t hit_chance = attack->accuracy;

    return (game_random(100) < hit_chance);
}

bool check_critical(const Tribomon *attacker) {
    if (!attacker) return false;

    // Base critical hit rate: 1/24 (~4.17%)
    // TODO: Implement high crit ratio moves and items
    uint8_t crit_chance = 4; // 4%

    return (game_random(100) < crit_chance);
}

// ====================================================================================
// STATUS EFFECTS
// ====================================================================================

bool apply_status_effect(Tribomon *target, StatusCondition status) {
    if (!target || status == STATUS_NONE) return false;

    // Can't apply status if already has one
    if (target->status != STATUS_NONE) {
        return false;
    }

    target->status = status;
    target->status_turns = 0;

    if (status == STATUS_SLEEP) {
        target->status_turns = game_random_range(1, 3); // 1-3 turns
    }

    const TribomonSpecies *species = get_species_data(target->species_id);
    ESP_LOGI(TAG, "%s was inflicted with %s!",
             species ? species->name : "Tribomon",
             get_status_name(status));

    return true;
}

bool can_attack(const Tribomon *mon) {
    if (!mon || mon->fainted) return false;

    switch (mon->status) {
        case STATUS_SLEEP:
            if (mon->status_turns > 0) {
                snprintf(battle_message, sizeof(battle_message),
                         "%s is fast asleep!", mon->nickname);
                return false;
            }
            break;

        case STATUS_FREEZE:
            // 20% chance to thaw
            if (game_random(100) >= 20) {
                snprintf(battle_message, sizeof(battle_message),
                         "%s is frozen solid!", mon->nickname);
                return false;
            }
            break;

        case STATUS_PARALYSIS:
            // 25% chance to be fully paralyzed
            if (game_random(100) < 25) {
                snprintf(battle_message, sizeof(battle_message),
                         "%s is paralyzed! It can't move!", mon->nickname);
                return false;
            }
            break;

        default:
            break;
    }

    return true;
}

void process_status_damage(Tribomon *mon) {
    if (!mon || mon->fainted) return;

    switch (mon->status) {
        case STATUS_BURN:
            // 1/16 HP damage
            tribomon_take_damage(mon, mon->stats.hp / 16);
            snprintf(battle_message, sizeof(battle_message),
                     "%s is hurt by its burn!", mon->nickname);
            break;

        case STATUS_POISON:
            // 1/8 HP damage
            tribomon_take_damage(mon, mon->stats.hp / 8);
            snprintf(battle_message, sizeof(battle_message),
                     "%s is hurt by poison!", mon->nickname);
            break;

        case STATUS_BADLY_POISONED:
            // n/16 HP damage (n = turns poisoned)
            mon->status_turns++;
            tribomon_take_damage(mon, (mon->stats.hp * mon->status_turns) / 16);
            snprintf(battle_message, sizeof(battle_message),
                     "%s is badly poisoned!", mon->nickname);
            break;

        case STATUS_SLEEP:
            if (mon->status_turns > 0) {
                mon->status_turns--;
                if (mon->status_turns == 0) {
                    mon->status = STATUS_NONE;
                    snprintf(battle_message, sizeof(battle_message),
                             "%s woke up!", mon->nickname);
                }
            }
            break;

        default:
            break;
    }
}

bool attempt_status_recovery(Tribomon *mon) {
    if (!mon) return false;

    // Only for freeze
    if (mon->status == STATUS_FREEZE) {
        if (game_random(100) < 20) {
            mon->status = STATUS_NONE;
            snprintf(battle_message, sizeof(battle_message),
                     "%s thawed out!", mon->nickname);
            return true;
        }
    }

    return false;
}

// ====================================================================================
// CAPTURE
// ====================================================================================

bool attempt_capture(const Tribomon *target, ItemType ball_type) {
    if (!target) return false;

    uint8_t catch_rate = calculate_capture_rate(target, ball_type);

    ESP_LOGI(TAG, "Capture attempt: rate=%d", catch_rate);

    // Shake checks (4 total)
    for (int i = 0; i < 4; i++) {
        uint16_t shake_check = (65535 * sqrt(sqrt((float)catch_rate / 255.0f))) / 65536;
        if (game_random(65536) >= shake_check) {
            ESP_LOGI(TAG, "Broke free on shake %d", i + 1);
            return false;
        }
    }

    return true;
}

uint8_t calculate_capture_rate(const Tribomon *target, ItemType ball_type) {
    if (!target) return 0;

    const TribomonSpecies *species = get_species_data(target->species_id);
    if (!species) return 0;

    // Base formula: ((3 * MaxHP - 2 * CurrentHP) * CatchRate * BallRate) / (3 * MaxHP)
    float hp_factor = (3.0f * target->stats.hp - 2.0f * target->current_hp) / (3.0f * target->stats.hp);
    float ball_bonus = get_ball_catch_rate(ball_type);
    float status_bonus = (target->status != STATUS_NONE) ? 1.5f : 1.0f;

    uint8_t rate = (uint8_t)((species->capture_rate * hp_factor * ball_bonus * status_bonus));

    return (rate > 255) ? 255 : rate;
}

float get_ball_catch_rate(ItemType ball_type) {
    switch (ball_type) {
        case ITEM_POKEBALL:     return 1.0f;
        case ITEM_GREATBALL:    return 1.5f;
        case ITEM_ULTRABALL:    return 2.0f;
        case ITEM_MASTERBALL:   return 255.0f; // Always succeeds
        default:                return 1.0f;
    }
}

// ====================================================================================
// AI SYSTEM
// ====================================================================================

BattleTurn battle_ai_generate_action(const Tribomon *enemy) {
    BattleTurn turn = {0};

    if (!enemy || enemy->attack_count == 0) {
        turn.action = BATTLE_ACTION_RUN;
        return turn;
    }

    // Simple AI: Choose attack with highest score
    int best_score = -1000;
    uint8_t best_attack = 0;

    BattleState *battle = battle_get_state();
    if (!battle) return turn;

    for (uint8_t i = 0; i < enemy->attack_count; i++) {
        if (enemy->pp_current[i] == 0) continue;

        int score = battle_ai_evaluate_attack(&enemy->attacks[i], enemy, battle->player_active);

        if (score > best_score) {
            best_score = score;
            best_attack = i;
        }
    }

    turn.action = BATTLE_ACTION_ATTACK;
    turn.data.attack_index = best_attack;

    return turn;
}

int battle_ai_evaluate_attack(const Attack *attack, const Tribomon *attacker,
                              const Tribomon *defender) {
    if (!attack || !attacker || !defender) return 0;

    int score = attack->power;

    // Consider type effectiveness
    const TribomonSpecies *def_species = get_species_data(defender->species_id);
    if (def_species) {
        uint8_t eff = get_type_effectiveness(attack->type, def_species->type1, def_species->type2);
        score = (score * eff) / 10;
    }

    // Prefer STAB
    const TribomonSpecies *atk_species = get_species_data(attacker->species_id);
    if (atk_species && (attack->type == atk_species->type1 || attack->type == atk_species->type2)) {
        score = (score * 3) / 2;
    }

    // Consider accuracy
    score = (score * attack->accuracy) / 100;

    return score;
}

// ====================================================================================
// REWARDS
// ====================================================================================

uint32_t calculate_exp_gain(const Tribomon *defeated, bool is_wild) {
    if (!defeated) return 0;

    const TribomonSpecies *species = get_species_data(defeated->species_id);
    if (!species) return 0;

    // Base formula: (BaseExp * Level) / 7
    uint32_t exp = (species->base_exp_yield * defeated->level) / 7;

    // Trainer battles give 1.5x EXP
    if (!is_wild) {
        exp = (exp * 3) / 2;
    }

    return exp;
}

uint32_t calculate_money_gain(uint16_t trainer_id, const Tribomon *defeated) {
    if (!defeated) return 0;

    // Simple formula: Level * 20
    return defeated->level * 20;
}

void distribute_experience(uint32_t exp_amount) {
    BattleState *battle = battle_get_state();
    if (!battle || !battle->player_active) return;

    // For now, only active Tribomon gains EXP
    // TODO: Implement EXP Share
    tribomon_gain_exp(battle->player_active, exp_amount);
}

// ====================================================================================
// UTILITY
// ====================================================================================

const char* battle_get_message(void) {
    return battle_message;
}

void battle_clear_message(void) {
    memset(battle_message, 0, sizeof(battle_message));
}

const char* battle_format_effectiveness(uint8_t effectiveness) {
    if (effectiveness >= EFFECTIVENESS_DOUBLE) {
        return "It's super effective!";
    } else if (effectiveness <= EFFECTIVENESS_HALF && effectiveness > EFFECTIVENESS_IMMUNE) {
        return "It's not very effective...";
    } else if (effectiveness == EFFECTIVENESS_IMMUNE) {
        return "It had no effect...";
    }
    return "";
}

const char* battle_format_critical(void) {
    return "A critical hit!";
}

bool battle_has_usable_tribomon(void) {
    return party_get_first_alive() != NULL;
}

bool battle_force_switch(void) {
    BattleState *battle = battle_get_state();
    if (!battle) return false;

    Tribomon *next = party_get_first_alive();
    if (!next) {
        return false; // All fainted
    }

    // Find slot
    Game *game = game_engine_get();
    for (uint8_t i = 0; i < game->player.party_count; i++) {
        if (&game->player.party[i] == next) {
            battle->player_active = next;
            battle->player_active_slot = i;
            snprintf(battle_message, sizeof(battle_message),
                     "Go, %s!", next->nickname);
            return true;
        }
    }

    return false;
}

uint16_t battle_get_turn_count(void) {
    BattleState *battle = battle_get_state();
    return battle ? battle->turn_count : 0;
}
