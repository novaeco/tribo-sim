/**
 * @file game_engine.h
 * @brief Tribomon Game Engine - Core Game Logic
 *
 * Manages game state, player data, party management, and game flow.
 */

#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "tribomon_types.h"
#include <stdbool.h>

// ====================================================================================
// GAME ENGINE INITIALIZATION
// ====================================================================================

/**
 * @brief Initialize the game engine
 * @return Pointer to global game state
 */
Game* game_engine_init(void);

/**
 * @brief Get current game state
 * @return Pointer to global game state
 */
Game* game_engine_get(void);

/**
 * @brief Start a new game
 * @param player_name Player's name (max 15 chars)
 * @param starter_id Species ID of starter Tribomon
 */
void game_new(const char *player_name, uint16_t starter_id);

/**
 * @brief Update game state (called every frame)
 */
void game_update(void);

// ====================================================================================
// SAVE/LOAD SYSTEM
// ====================================================================================

/**
 * @brief Save game to NVS (Non-Volatile Storage)
 * @param slot Save slot (0-2)
 * @return true on success
 */
bool game_save(uint8_t slot);

/**
 * @brief Load game from NVS
 * @param slot Save slot (0-2)
 * @return true on success
 */
bool game_load(uint8_t slot);

/**
 * @brief Check if save slot has data
 * @param slot Save slot (0-2)
 * @return true if slot has valid save data
 */
bool game_save_exists(uint8_t slot);

/**
 * @brief Delete save data
 * @param slot Save slot (0-2)
 * @return true on success
 */
bool game_delete_save(uint8_t slot);

// ====================================================================================
// PARTY MANAGEMENT
// ====================================================================================

/**
 * @brief Add Tribomon to player's party
 * @param mon Tribomon to add (will be copied)
 * @return true if added successfully (party not full)
 */
bool party_add_tribomon(const Tribomon *mon);

/**
 * @brief Remove Tribomon from party
 * @param slot Party slot (0-5)
 * @return true if removed successfully
 */
bool party_remove_tribomon(uint8_t slot);

/**
 * @brief Swap two Tribomon in party
 * @param slot1 First party slot (0-5)
 * @param slot2 Second party slot (0-5)
 */
void party_swap(uint8_t slot1, uint8_t slot2);

/**
 * @brief Get Tribomon at party slot
 * @param slot Party slot (0-5)
 * @return Pointer to Tribomon, NULL if slot empty
 */
Tribomon* party_get_tribomon(uint8_t slot);

/**
 * @brief Get first non-fainted Tribomon
 * @return Pointer to Tribomon, NULL if all fainted
 */
Tribomon* party_get_first_alive(void);

/**
 * @brief Check if entire party is fainted
 * @return true if all Tribomon fainted
 */
bool party_all_fainted(void);

/**
 * @brief Heal entire party to full HP
 */
void party_heal_all(void);

// ====================================================================================
// INVENTORY MANAGEMENT
// ====================================================================================

/**
 * @brief Add item to inventory
 * @param item_type Type of item to add
 * @param quantity Number to add
 * @return true if added successfully
 */
bool inventory_add_item(ItemType item_type, uint16_t quantity);

/**
 * @brief Remove item from inventory
 * @param item_type Type of item to remove
 * @param quantity Number to remove
 * @return true if removed successfully (had enough items)
 */
bool inventory_remove_item(ItemType item_type, uint16_t quantity);

/**
 * @brief Get quantity of item in inventory
 * @param item_type Type of item to check
 * @return Quantity available
 */
uint16_t inventory_get_quantity(ItemType item_type);

/**
 * @brief Use item on Tribomon
 * @param item_type Type of item to use
 * @param target_slot Party slot of target Tribomon (0-5)
 * @return true if item was used successfully
 */
bool inventory_use_item(ItemType item_type, uint8_t target_slot);

/**
 * @brief Get item name
 * @param item_type Type of item
 * @return Item name string
 */
const char* inventory_get_item_name(ItemType item_type);

/**
 * @brief Get item description
 * @param item_type Type of item
 * @return Item description string
 */
const char* inventory_get_item_description(ItemType item_type);

/**
 * @brief Get item price
 * @param item_type Type of item
 * @return Price in Pokedollars
 */
uint32_t inventory_get_item_price(ItemType item_type);

// ====================================================================================
// RANDOM ENCOUNTERS
// ====================================================================================

/**
 * @brief Process player step (for random encounters)
 * @return true if encounter triggered
 */
bool encounter_step(void);

/**
 * @brief Generate wild Tribomon encounter
 * @param area_id Area/route ID for encounter table
 * @param wild_mon Output pointer for generated Tribomon
 */
void encounter_generate_wild(uint8_t area_id, Tribomon *wild_mon);

/**
 * @brief Set encounter rate multiplier
 * @param multiplier Multiplier (0.0 = no encounters, 1.0 = normal, 2.0 = double)
 */
void encounter_set_rate(float multiplier);

// ====================================================================================
// POKEDEX
// ====================================================================================

/**
 * @brief Register Tribomon as seen in Pokedex
 * @param species_id Species ID
 */
void pokedex_register_seen(uint16_t species_id);

/**
 * @brief Register Tribomon as caught in Pokedex
 * @param species_id Species ID
 */
void pokedex_register_caught(uint16_t species_id);

/**
 * @brief Check if species has been seen
 * @param species_id Species ID
 * @return true if seen
 */
bool pokedex_is_seen(uint16_t species_id);

/**
 * @brief Check if species has been caught
 * @param species_id Species ID
 * @return true if caught
 */
bool pokedex_is_caught(uint16_t species_id);

/**
 * @brief Get Pokedex completion percentage
 * @return Percentage (0.0 - 100.0)
 */
float pokedex_get_completion(void);

// ====================================================================================
// MONEY & ECONOMY
// ====================================================================================

/**
 * @brief Add money to player
 * @param amount Amount to add
 */
void player_add_money(uint32_t amount);

/**
 * @brief Remove money from player
 * @param amount Amount to remove
 * @return true if player had enough money
 */
bool player_remove_money(uint32_t amount);

/**
 * @brief Get player's current money
 * @return Money amount
 */
uint32_t player_get_money(void);

// ====================================================================================
// UTILITY FUNCTIONS
// ====================================================================================

/**
 * @brief Get current play time
 * @return Play time in seconds
 */
uint32_t game_get_play_time(void);

/**
 * @brief Format play time as string
 * @param buffer Output buffer (min 16 bytes)
 * @param buffer_size Size of buffer
 */
void game_format_play_time(char *buffer, size_t buffer_size);

/**
 * @brief Get random number (0 - max-1)
 * @param max Maximum value (exclusive)
 * @return Random number
 */
uint32_t game_random(uint32_t max);

/**
 * @brief Get random number in range
 * @param min Minimum value (inclusive)
 * @param max Maximum value (inclusive)
 * @return Random number
 */
uint32_t game_random_range(uint32_t min, uint32_t max);

/**
 * @brief Seed random number generator
 * @param seed Seed value
 */
void game_seed_random(uint32_t seed);

#endif // GAME_ENGINE_H
