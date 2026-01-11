/**
 * @file ui_game.c
 * @brief Tribomon Game UI Implementation (Minimal Version)
 *
 * This is a minimal implementation focusing on core functionality.
 * Full UI with animations and polish to be added later.
 */

#include "ui_game.h"
#include "game_engine.h"
#include "battle_system.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_GAME";

// UI Containers
static lv_obj_t *ui_root = NULL;
static lv_obj_t *ui_menu_screen = NULL;
static lv_obj_t *ui_party_screen = NULL;
static lv_obj_t *ui_battle_screen = NULL;
static lv_obj_t *ui_inventory_screen = NULL;

// UI State
static GameState current_ui_state = GAME_STATE_MENU;

// ====================================================================================
// INITIALIZATION
// ====================================================================================

void ui_game_init(lv_obj_t *parent) {
    if (!parent) {
        ESP_LOGE(TAG, "Invalid parent object");
        return;
    }

    ui_root = parent;

    // Create screens (initially hidden)
    ui_game_show_menu();

    ESP_LOGI(TAG, "Game UI initialized");
}

void ui_game_update(void) {
    Game *game = game_engine_get();
    if (!game) return;

    // Update UI based on game state
    if (game->current_state != current_ui_state) {
        current_ui_state = game->current_state;

        switch (current_ui_state) {
            case GAME_STATE_MENU:
                ui_game_show_menu();
                break;
            case GAME_STATE_PARTY:
                ui_game_show_party();
                break;
            case GAME_STATE_BATTLE:
                ui_game_show_battle();
                break;
            case GAME_STATE_INVENTORY:
                ui_game_show_inventory();
                break;
            default:
                break;
        }
    }

    // Update active screen
    if (current_ui_state == GAME_STATE_BATTLE) {
        ui_battle_update();
    }
}

void ui_game_set_visible(bool visible) {
    if (ui_root) {
        if (visible) {
            lv_obj_clear_flag(ui_root, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_root, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ====================================================================================
// SCREEN NAVIGATION
// ====================================================================================

static void hide_all_screens(void) {
    if (ui_menu_screen) lv_obj_add_flag(ui_menu_screen, LV_OBJ_FLAG_HIDDEN);
    if (ui_party_screen) lv_obj_add_flag(ui_party_screen, LV_OBJ_FLAG_HIDDEN);
    if (ui_battle_screen) lv_obj_add_flag(ui_battle_screen, LV_OBJ_FLAG_HIDDEN);
    if (ui_inventory_screen) lv_obj_add_flag(ui_inventory_screen, LV_OBJ_FLAG_HIDDEN);
}

void ui_game_show_menu(void) {
    hide_all_screens();

    if (!ui_menu_screen) {
        // Create menu screen
        ui_menu_screen = lv_obj_create(ui_root);
        lv_obj_set_size(ui_menu_screen, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(ui_menu_screen, lv_color_hex(0x1A1A2E), 0);

        lv_obj_t *title = lv_label_create(ui_menu_screen);
        lv_label_set_text(title, "TRIBOMON");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFD700), 0);
        lv_obj_center(title);

        lv_obj_t *subtitle = lv_label_create(ui_menu_screen);
        lv_label_set_text(subtitle, "Press any button to start");
        lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    }

    lv_obj_clear_flag(ui_menu_screen, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Showing menu screen");
}

void ui_game_show_overworld(void) {
    ESP_LOGI(TAG, "Overworld UI not yet implemented");
    // TODO: Implement overworld UI
}

void ui_game_show_party(void) {
    hide_all_screens();

    if (ui_party_screen) {
        lv_obj_del(ui_party_screen);
    }

    // Create party screen
    ui_party_screen = lv_obj_create(ui_root);
    lv_obj_set_size(ui_party_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui_party_screen, lv_color_hex(0x0F3460), 0);
    lv_obj_set_flex_flow(ui_party_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_party_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ui_party_screen, 10, 0);

    // Title
    lv_obj_t *title = lv_label_create(ui_party_screen);
    lv_label_set_text(title, "PARTY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    // Get party
    Game *game = game_engine_get();
    if (!game) return;

    for (uint8_t i = 0; i < game->player.party_count; i++) {
        ui_party_create_card(ui_party_screen, &game->player.party[i], i);
    }

    lv_obj_clear_flag(ui_party_screen, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Showing party screen");
}

void ui_game_show_inventory(void) {
    hide_all_screens();

    if (ui_inventory_screen) {
        lv_obj_del(ui_inventory_screen);
    }

    // Create inventory screen
    ui_inventory_screen = lv_obj_create(ui_root);
    lv_obj_set_size(ui_inventory_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui_inventory_screen, lv_color_hex(0x16213E), 0);

    lv_obj_t *title = lv_label_create(ui_inventory_screen);
    lv_label_set_text(title, "INVENTORY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // List items
    Game *game = game_engine_get();
    if (!game) return;

    lv_obj_t *list = lv_list_create(ui_inventory_screen);
    lv_obj_set_size(list, LV_PCT(90), LV_PCT(80));
    lv_obj_center(list);

    for (uint8_t i = 0; i < game->player.inventory_count; i++) {
        char text[64];
        snprintf(text, sizeof(text), "%s x%d",
                 inventory_get_item_name(game->player.inventory[i].type),
                 game->player.inventory[i].quantity);

        lv_list_add_text(list, text);
    }

    lv_obj_clear_flag(ui_inventory_screen, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Showing inventory screen");
}

void ui_game_show_battle(void) {
    hide_all_screens();

    if (ui_battle_screen) {
        lv_obj_del(ui_battle_screen);
    }

    // Create battle screen
    ui_battle_screen = lv_obj_create(ui_root);
    lv_obj_set_size(ui_battle_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui_battle_screen, lv_color_hex(0x0A0E27), 0);

    // Battle title
    lv_obj_t *title = lv_label_create(ui_battle_screen);
    lv_label_set_text(title, "BATTLE!");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    ui_battle_update();

    lv_obj_clear_flag(ui_battle_screen, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Showing battle screen");
}

void ui_game_show_tribomon_detail(uint8_t party_slot) {
    ESP_LOGI(TAG, "Tribomon detail UI not yet implemented");
    // TODO: Implement detail screen
}

void ui_game_show_pokedex(void) {
    ESP_LOGI(TAG, "Pokedex UI not yet implemented");
    // TODO: Implement Pokedex
}

// ====================================================================================
// BATTLE UI
// ====================================================================================

void ui_battle_update(void) {
    BattleState *battle = battle_get_state();
    if (!battle || !ui_battle_screen) return;

    // Clear previous content (keep title)
    lv_obj_clean(ui_battle_screen);

    // Redraw title
    lv_obj_t *title = lv_label_create(ui_battle_screen);
    lv_label_set_text(title, "BATTLE!");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Enemy Tribomon
    if (battle->enemy_active.species_id > 0) {
        const TribomonSpecies *species = get_species_data(battle->enemy_active.species_id);

        lv_obj_t *enemy_card = lv_obj_create(ui_battle_screen);
        lv_obj_set_size(enemy_card, 300, 100);
        lv_obj_align(enemy_card, LV_ALIGN_TOP_RIGHT, -10, 60);
        lv_obj_set_style_bg_color(enemy_card, lv_color_hex(0xFF6B6B), 0);

        lv_obj_t *enemy_name = lv_label_create(enemy_card);
        lv_label_set_text_fmt(enemy_name, "%s Lv%d",
                              species ? species->name : "???",
                              battle->enemy_active.level);
        lv_obj_align(enemy_name, LV_ALIGN_TOP_LEFT, 5, 5);

        // HP bar
        lv_obj_t *hp_bar = ui_create_hp_bar(enemy_card, battle->enemy_active.current_hp,
                                             battle->enemy_active.stats.hp);
        lv_obj_align(hp_bar, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    }

    // Player Tribomon
    if (battle->player_active) {
        const TribomonSpecies *species = get_species_data(battle->player_active->species_id);

        lv_obj_t *player_card = lv_obj_create(ui_battle_screen);
        lv_obj_set_size(player_card, 300, 120);
        lv_obj_align(player_card, LV_ALIGN_BOTTOM_LEFT, 10, -100);
        lv_obj_set_style_bg_color(player_card, lv_color_hex(0x4ECDC4), 0);

        lv_obj_t *player_name = lv_label_create(player_card);
        lv_label_set_text_fmt(player_name, "%s Lv%d",
                              battle->player_active->nickname,
                              battle->player_active->level);
        lv_obj_align(player_name, LV_ALIGN_TOP_LEFT, 5, 5);

        // HP bar
        lv_obj_t *hp_bar = ui_create_hp_bar(player_card, battle->player_active->current_hp,
                                             battle->player_active->stats.hp);
        lv_obj_align(hp_bar, LV_ALIGN_BOTTOM_LEFT, 5, -30);

        // HP text
        lv_obj_t *hp_text = lv_label_create(player_card);
        lv_label_set_text_fmt(hp_text, "HP: %d/%d",
                              battle->player_active->current_hp,
                              battle->player_active->stats.hp);
        lv_obj_align(hp_text, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    }

    // Battle message
    const char *msg = battle_get_message();
    if (msg && strlen(msg) > 0) {
        lv_obj_t *msg_box = lv_obj_create(ui_battle_screen);
        lv_obj_set_size(msg_box, LV_PCT(90), 80);
        lv_obj_align(msg_box, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(msg_box, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_color(msg_box, lv_color_hex(0xFFFFFF), 0);

        lv_obj_t *msg_label = lv_label_create(msg_box);
        lv_label_set_text(msg_label, msg);
        lv_obj_set_style_text_color(msg_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(msg_label);
    }
}

void ui_battle_show_action_menu(void) {
    ESP_LOGI(TAG, "Battle action menu not yet implemented");
    // TODO: Implement action menu buttons
}

void ui_battle_show_attack_menu(void) {
    ESP_LOGI(TAG, "Attack menu not yet implemented");
    // TODO: Implement attack selection
}

void ui_battle_show_switch_menu(void) {
    ESP_LOGI(TAG, "Switch menu not yet implemented");
    // TODO: Implement switch menu
}

void ui_battle_show_message(const char *message, uint32_t duration_ms) {
    ESP_LOGI(TAG, "Battle message: %s", message);
}

void ui_battle_hide_menus(void) {
    // TODO: Implement menu hiding
}

// ====================================================================================
// PARTY UI
// ====================================================================================

void ui_party_refresh(void) {
    if (current_ui_state == GAME_STATE_PARTY) {
        ui_game_show_party();
    }
}

lv_obj_t* ui_party_create_card(lv_obj_t *parent, const Tribomon *mon, uint8_t slot) {
    if (!parent || !mon) return NULL;

    const TribomonSpecies *species = get_species_data(mon->species_id);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(95), 80);
    lv_obj_set_style_bg_color(card, ui_get_type_color(species ? species->type1 : TYPE_NORMAL), 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    // Name and level
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text_fmt(name, "%s Lv%d", mon->nickname, mon->level);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    // HP
    lv_obj_t *hp_label = lv_label_create(card);
    lv_label_set_text_fmt(hp_label, "HP: %d/%d", mon->current_hp, mon->stats.hp);
    lv_obj_align(hp_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    // HP bar
    lv_obj_t *hp_bar = ui_create_hp_bar(card, mon->current_hp, mon->stats.hp);
    lv_obj_set_size(hp_bar, LV_PCT(90), 10);
    lv_obj_align(hp_bar, LV_ALIGN_BOTTOM_MID, 0, -5);

    // Status
    if (mon->status != STATUS_NONE) {
        lv_obj_t *status_label = lv_label_create(card);
        lv_label_set_text(status_label, ui_get_status_abbr(mon->status));
        lv_obj_set_style_bg_color(status_label, ui_get_status_color(mon->status), 0);
        lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, 0);
        lv_obj_align(status_label, LV_ALIGN_TOP_RIGHT, 0, 25);
    }

    return card;
}

// ====================================================================================
// INVENTORY UI
// ====================================================================================

void ui_inventory_refresh(void) {
    if (current_ui_state == GAME_STATE_INVENTORY) {
        ui_game_show_inventory();
    }
}

void ui_inventory_filter(uint8_t category) {
    // TODO: Implement filtering
}

// ====================================================================================
// UTILITY FUNCTIONS
// ====================================================================================

lv_color_t ui_get_type_color(TribomonType type) {
    static const uint32_t type_colors[] = {
        0xA8A878, // Normal
        0xF08030, // Fire
        0x6890F0, // Water
        0x78C850, // Grass
        0xF8D030, // Electric
        0x98D8D8, // Ice
        0xC03028, // Fighting
        0xA040A0, // Poison
        0xE0C068, // Ground
        0xA890F0, // Flying
        0xF85888, // Psychic
        0xA8B820, // Bug
        0xB8A038, // Rock
        0x705898, // Ghost
        0x7038F8, // Dragon
        0x705848, // Dark
        0xB8B8D0, // Steel
        0xEE99AC  // Fairy
    };

    return lv_color_hex((type < TYPE_COUNT) ? type_colors[type] : 0x888888);
}

const char* ui_get_type_icon(TribomonType type) {
    static const char *icons[] = {
        LV_SYMBOL_STOP,    // Normal
        LV_SYMBOL_POWER,   // Fire
        LV_SYMBOL_REFRESH, // Water
        LV_SYMBOL_IMAGE,   // Grass
        LV_SYMBOL_CHARGE,  // Electric
        LV_SYMBOL_WARNING, // Ice
        LV_SYMBOL_UP,      // Fighting
        LV_SYMBOL_CLOSE,   // Poison
        LV_SYMBOL_DOWN,    // Ground
        LV_SYMBOL_UPLOAD,  // Flying
        LV_SYMBOL_EYE_OPEN,// Psychic
        LV_SYMBOL_SETTINGS,// Bug
        LV_SYMBOL_SAVE,    // Rock
        LV_SYMBOL_WIFI,    // Ghost
        LV_SYMBOL_BELL,    // Dragon
        LV_SYMBOL_BLUETOOTH,// Dark
        LV_SYMBOL_GPS,     // Steel
        LV_SYMBOL_BATTERY_FULL // Fairy
    };

    return (type < TYPE_COUNT) ? icons[type] : LV_SYMBOL_STOP;
}

void ui_format_hp_text(lv_obj_t *label, uint16_t current_hp, uint16_t max_hp) {
    if (!label) return;

    float hp_percent = (float)current_hp / max_hp;
    lv_color_t color;

    if (hp_percent > 0.5f) {
        color = lv_color_hex(0x00FF00); // Green
    } else if (hp_percent > 0.2f) {
        color = lv_color_hex(0xFFFF00); // Yellow
    } else {
        color = lv_color_hex(0xFF0000); // Red
    }

    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text_fmt(label, "%d/%d", current_hp, max_hp);
}

lv_obj_t* ui_create_hp_bar(lv_obj_t *parent, uint16_t current_hp, uint16_t max_hp) {
    if (!parent) return NULL;

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 200, 15);
    lv_bar_set_range(bar, 0, max_hp);
    lv_bar_set_value(bar, current_hp, LV_ANIM_OFF);

    // Color based on HP percentage
    float hp_percent = (float)current_hp / max_hp;
    if (hp_percent > 0.5f) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    } else if (hp_percent > 0.2f) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    }

    return bar;
}

void ui_update_hp_bar(lv_obj_t *bar, uint16_t current_hp, uint16_t max_hp) {
    if (!bar) return;

    lv_bar_set_value(bar, current_hp, LV_ANIM_ON);

    float hp_percent = (float)current_hp / max_hp;
    if (hp_percent > 0.5f) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    } else if (hp_percent > 0.2f) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    }
}

lv_obj_t* ui_create_exp_bar(lv_obj_t *parent, uint32_t current_exp, uint32_t exp_to_next) {
    if (!parent) return NULL;

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 200, 10);
    lv_bar_set_range(bar, 0, exp_to_next);
    lv_bar_set_value(bar, current_exp, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00BFFF), LV_PART_INDICATOR);

    return bar;
}

lv_color_t ui_get_status_color(StatusCondition status) {
    static const uint32_t status_colors[] = {
        0x888888, // None (gray)
        0xFF4500, // Burn (orange-red)
        0x87CEEB, // Freeze (light blue)
        0xFFFF00, // Paralysis (yellow)
        0x9370DB, // Poison (purple)
        0x778899, // Sleep (gray)
        0x8B008B  // Badly Poisoned (dark purple)
    };

    return lv_color_hex((status < STATUS_COUNT) ? status_colors[status] : 0x888888);
}

const char* ui_get_status_abbr(StatusCondition status) {
    static const char *abbr[] = {
        "", "BRN", "FRZ", "PAR", "PSN", "SLP", "TOX"
    };

    return (status < STATUS_COUNT) ? abbr[status] : "";
}

// ====================================================================================
// DIALOGS (Stub implementations)
// ====================================================================================

void ui_show_confirm_dialog(const char *title, const char *message, void (*callback)(void)) {
    ESP_LOGI(TAG, "Confirm dialog: %s - %s", title, message);
    // TODO: Implement dialog
}

void ui_show_message_box(const char *title, const char *message, uint32_t auto_close) {
    ESP_LOGI(TAG, "Message box: %s - %s", title, message);
    // TODO: Implement message box
}

void ui_close_dialog(void) {
    // TODO: Implement dialog closing
}
