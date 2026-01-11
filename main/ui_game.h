/**
 * @file ui_game.h
 * @brief Tribomon Game UI - LVGL Interface
 *
 * User interface for the Tribomon game using LVGL.
 */

#ifndef UI_GAME_H
#define UI_GAME_H

#include "lvgl.h"
#include "tribomon_types.h"
#include <stdbool.h>

// ====================================================================================
// UI INITIALIZATION
// ====================================================================================

/**
 * @brief Initialize game UI system
 * @param parent Parent screen object
 */
void ui_game_init(lv_obj_t *parent);

/**
 * @brief Update game UI (called periodically)
 */
void ui_game_update(void);

/**
 * @brief Show/hide game UI
 * @param visible true to show, false to hide
 */
void ui_game_set_visible(bool visible);

// ====================================================================================
// SCREEN NAVIGATION
// ====================================================================================

/**
 * @brief Show main menu screen
 */
void ui_game_show_menu(void);

/**
 * @brief Show overworld/map screen
 */
void ui_game_show_overworld(void);

/**
 * @brief Show party screen (6 Tribomon)
 */
void ui_game_show_party(void);

/**
 * @brief Show inventory screen
 */
void ui_game_show_inventory(void);

/**
 * @brief Show battle screen
 */
void ui_game_show_battle(void);

/**
 * @brief Show Tribomon detail screen
 * @param party_slot Party slot (0-5)
 */
void ui_game_show_tribomon_detail(uint8_t party_slot);

/**
 * @brief Show Pokedex screen
 */
void ui_game_show_pokedex(void);

// ====================================================================================
// BATTLE UI
// ====================================================================================

/**
 * @brief Update battle UI with current state
 */
void ui_battle_update(void);

/**
 * @brief Show battle action menu (Attack/Item/Switch/Run)
 */
void ui_battle_show_action_menu(void);

/**
 * @brief Show attack selection menu
 */
void ui_battle_show_attack_menu(void);

/**
 * @brief Show party switch menu
 */
void ui_battle_show_switch_menu(void);

/**
 * @brief Show battle message
 * @param message Message text
 * @param duration_ms Display duration (0 = until clicked)
 */
void ui_battle_show_message(const char *message, uint32_t duration_ms);

/**
 * @brief Hide all battle menus
 */
void ui_battle_hide_menus(void);

// ====================================================================================
// PARTY UI
// ====================================================================================

/**
 * @brief Refresh party display
 */
void ui_party_refresh(void);

/**
 * @brief Create Tribomon card widget
 * @param parent Parent container
 * @param mon Tribomon to display
 * @param slot Party slot
 * @return Card object
 */
lv_obj_t* ui_party_create_card(lv_obj_t *parent, const Tribomon *mon, uint8_t slot);

// ====================================================================================
// INVENTORY UI
// ====================================================================================

/**
 * @brief Refresh inventory display
 */
void ui_inventory_refresh(void);

/**
 * @brief Filter inventory by category
 * @param category Item category (0=All, 1=Balls, 2=Medicine, 3=Items)
 */
void ui_inventory_filter(uint8_t category);

// ====================================================================================
// DIALOGS & POPUPS
// ====================================================================================

/**
 * @brief Show confirmation dialog
 * @param title Dialog title
 * @param message Dialog message
 * @param callback Callback function when confirmed
 */
void ui_show_confirm_dialog(const char *title, const char *message, void (*callback)(void));

/**
 * @brief Show message box
 * @param title Box title
 * @param message Box message
 * @param auto_close Auto-close after N milliseconds (0 = manual)
 */
void ui_show_message_box(const char *title, const char *message, uint32_t auto_close);

/**
 * @brief Close active dialog/message box
 */
void ui_close_dialog(void);

// ====================================================================================
// UTILITY FUNCTIONS
// ====================================================================================

/**
 * @brief Get type color for UI
 * @param type Tribomon type
 * @return LVGL color
 */
lv_color_t ui_get_type_color(TribomonType type);

/**
 * @brief Get type icon/emoji
 * @param type Tribomon type
 * @return Icon string
 */
const char* ui_get_type_icon(TribomonType type);

/**
 * @brief Format HP text with color
 * @param label Label object
 * @param current_hp Current HP
 * @param max_hp Maximum HP
 */
void ui_format_hp_text(lv_obj_t *label, uint16_t current_hp, uint16_t max_hp);

/**
 * @brief Create HP bar widget
 * @param parent Parent container
 * @param current_hp Current HP
 * @param max_hp Maximum HP
 * @return HP bar object
 */
lv_obj_t* ui_create_hp_bar(lv_obj_t *parent, uint16_t current_hp, uint16_t max_hp);

/**
 * @brief Update HP bar
 * @param bar HP bar object
 * @param current_hp Current HP
 * @param max_hp Maximum HP
 */
void ui_update_hp_bar(lv_obj_t *bar, uint16_t current_hp, uint16_t max_hp);

/**
 * @brief Create EXP bar widget
 * @param parent Parent container
 * @param current_exp Current EXP
 * @param exp_to_next EXP needed for next level
 * @return EXP bar object
 */
lv_obj_t* ui_create_exp_bar(lv_obj_t *parent, uint32_t current_exp, uint32_t exp_to_next);

/**
 * @brief Get status condition color
 * @param status Status condition
 * @return LVGL color
 */
lv_color_t ui_get_status_color(StatusCondition status);

/**
 * @brief Get status condition abbreviation
 * @param status Status condition
 * @return 3-letter abbreviation (BRN, FRZ, PAR, PSN, SLP)
 */
const char* ui_get_status_abbr(StatusCondition status);

#endif // UI_GAME_H
