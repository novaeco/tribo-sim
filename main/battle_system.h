/**
 * @file battle_system.h
 * @brief Tribomon Battle System - Turn-based Combat
 *
 * Handles turn-based battles, damage calculation, status effects,
 * and capture mechanics.
 */

#ifndef BATTLE_SYSTEM_H
#define BATTLE_SYSTEM_H

#include "tribomon_types.h"
#include "game_engine.h"
#include <stdbool.h>

// ====================================================================================
// BATTLE INITIALIZATION
// ====================================================================================

/**
 * @brief Start a wild Tribomon battle
 * @param wild_tribomon Pointer to wild Tribomon
 * @return Pointer to battle state
 */
BattleState* battle_start_wild(Tribomon *wild_tribomon);

/**
 * @brief Start a trainer battle
 * @param trainer_tribomon Pointer to trainer's Tribomon
 * @param trainer_id Trainer ID
 * @return Pointer to battle state
 */
BattleState* battle_start_trainer(Tribomon *trainer_tribomon, uint16_t trainer_id);

/**
 * @brief Get current battle state
 * @return Pointer to battle state, NULL if no battle active
 */
BattleState* battle_get_state(void);

/**
 * @brief End current battle
 * @param player_won true if player won
 */
void battle_end(bool player_won);

/**
 * @brief Check if battle is active
 * @return true if battle ongoing
 */
bool battle_is_active(void);

// ====================================================================================
// TURN MANAGEMENT
// ====================================================================================

/**
 * @brief Player chooses to attack
 * @param attack_index Index of attack (0-3)
 * @return true if action valid
 */
bool battle_player_attack(uint8_t attack_index);

/**
 * @brief Player uses an item
 * @param item_type Type of item
 * @param target_slot Party slot (for healing items)
 * @return true if item used successfully
 */
bool battle_player_use_item(ItemType item_type, uint8_t target_slot);

/**
 * @brief Player switches Tribomon
 * @param party_slot Party slot to switch to (0-5)
 * @return true if switch valid
 */
bool battle_player_switch(uint8_t party_slot);

/**
 * @brief Player attempts to run from battle
 * @return true if escape successful
 */
bool battle_player_run(void);

/**
 * @brief Execute the current turn
 *
 * Determines turn order based on speed, executes both actions,
 * applies status effects, and checks for battle end conditions.
 *
 * @return true if battle continues, false if battle ended
 */
bool battle_execute_turn(void);

// ====================================================================================
// COMBAT MECHANICS
// ====================================================================================

/**
 * @brief Calculate damage for an attack
 * @param attacker Attacking Tribomon
 * @param defender Defending Tribomon
 * @param attack Attack being used
 * @param is_critical Output: true if critical hit
 * @return Damage amount
 */
uint16_t calculate_damage(const Tribomon *attacker, const Tribomon *defender,
                          const Attack *attack, bool *is_critical);

/**
 * @brief Apply stat stage modifier
 * @param stat Base stat value
 * @param stage Stat stage (-6 to +6)
 * @return Modified stat value
 */
uint16_t apply_stat_modifier(uint16_t stat, int8_t stage);

/**
 * @brief Check if attack hits
 * @param attack Attack being used
 * @param attacker Attacker (for accuracy stages)
 * @param defender Defender (for evasion stages)
 * @return true if attack hits
 */
bool check_hit(const Attack *attack, const Tribomon *attacker, const Tribomon *defender);

/**
 * @brief Check if attack is a critical hit
 * @param attacker Attacker
 * @return true if critical hit
 */
bool check_critical(const Tribomon *attacker);

// ====================================================================================
// STATUS EFFECTS
// ====================================================================================

/**
 * @brief Apply status condition to Tribomon
 * @param target Target Tribomon
 * @param status Status to apply
 * @return true if status applied successfully
 */
bool apply_status_effect(Tribomon *target, StatusCondition status);

/**
 * @brief Check if Tribomon can attack (status check)
 * @param mon Tribomon to check
 * @return true if can attack, false if status prevents it
 */
bool can_attack(const Tribomon *mon);

/**
 * @brief Process status damage (burn, poison, etc.)
 * @param mon Tribomon with status
 */
void process_status_damage(Tribomon *mon);

/**
 * @brief Attempt to cure status (for sleep/freeze)
 * @param mon Tribomon with status
 * @return true if status cured
 */
bool attempt_status_recovery(Tribomon *mon);

// ====================================================================================
// CAPTURE MECHANICS
// ====================================================================================

/**
 * @brief Attempt to capture wild Tribomon
 * @param target Wild Tribomon to capture
 * @param ball_type Type of Poke Ball used
 * @return true if capture successful
 */
bool attempt_capture(const Tribomon *target, ItemType ball_type);

/**
 * @brief Calculate capture probability
 * @param target Wild Tribomon
 * @param ball_type Type of Poke Ball
 * @return Capture probability (0-255)
 */
uint8_t calculate_capture_rate(const Tribomon *target, ItemType ball_type);

/**
 * @brief Get ball catch rate multiplier
 * @param ball_type Type of Poke Ball
 * @return Catch rate multiplier (1.0 for Poke Ball, 1.5 for Great Ball, etc.)
 */
float get_ball_catch_rate(ItemType ball_type);

// ====================================================================================
// AI SYSTEM
// ====================================================================================

/**
 * @brief Generate enemy AI action
 * @param enemy Enemy Tribomon
 * @return Battle action for enemy
 */
BattleTurn battle_ai_generate_action(const Tribomon *enemy);

/**
 * @brief Calculate attack effectiveness score for AI
 * @param attack Attack to evaluate
 * @param attacker Attacking Tribomon
 * @param defender Defending Tribomon
 * @return Score (higher = better choice)
 */
int battle_ai_evaluate_attack(const Attack *attack, const Tribomon *attacker,
                              const Tribomon *defender);

// ====================================================================================
// EXPERIENCE & REWARDS
// ====================================================================================

/**
 * @brief Calculate experience gained from defeating Tribomon
 * @param defeated Defeated Tribomon
 * @param is_wild true if wild, false if trainer battle
 * @return Experience points
 */
uint32_t calculate_exp_gain(const Tribomon *defeated, bool is_wild);

/**
 * @brief Calculate money gained from trainer battle
 * @param trainer_id Trainer ID
 * @param defeated Defeated Tribomon
 * @return Money amount
 */
uint32_t calculate_money_gain(uint16_t trainer_id, const Tribomon *defeated);

/**
 * @brief Distribute experience to party
 * @param exp_amount Total experience to distribute
 */
void distribute_experience(uint32_t exp_amount);

// ====================================================================================
// BATTLE TEXT/LOG
// ====================================================================================

/**
 * @brief Get battle message buffer
 * @return Pointer to current battle message
 */
const char* battle_get_message(void);

/**
 * @brief Clear battle message
 */
void battle_clear_message(void);

/**
 * @brief Format effectiveness text
 * @param effectiveness Type effectiveness value
 * @return Text ("It's super effective!", "It's not very effective...", etc.)
 */
const char* battle_format_effectiveness(uint8_t effectiveness);

/**
 * @brief Format critical hit text
 * @return "A critical hit!"
 */
const char* battle_format_critical(void);

// ====================================================================================
// UTILITY
// ====================================================================================

/**
 * @brief Check if player has any usable Tribomon
 * @return true if at least one non-fainted Tribomon
 */
bool battle_has_usable_tribomon(void);

/**
 * @brief Force switch to next alive Tribomon
 * @return true if switch successful, false if all fainted
 */
bool battle_force_switch(void);

/**
 * @brief Get battle turn count
 * @return Number of turns elapsed
 */
uint16_t battle_get_turn_count(void);

#endif // BATTLE_SYSTEM_H
