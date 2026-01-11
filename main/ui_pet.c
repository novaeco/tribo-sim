/**
 * @file ui_pet.c
 * @brief Implémentation de l'interface LVGL pour le simulateur de Tribolonotus
 */

#include "ui_pet.h"
#include "pet_simulator.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_PET";

// ====================================================================================
// VARIABLES GLOBALES UI
// ====================================================================================

static lv_obj_t *g_main_screen = NULL;
static lv_obj_t *g_pet_image = NULL;
static lv_obj_t *g_pet_name_label = NULL;
static lv_obj_t *g_pet_info_label = NULL;

// Barres de besoins
static lv_obj_t *g_hunger_bar = NULL;
static lv_obj_t *g_thirst_bar = NULL;
static lv_obj_t *g_temp_bar = NULL;
static lv_obj_t *g_humidity_bar = NULL;
static lv_obj_t *g_clean_bar = NULL;
static lv_obj_t *g_happy_bar = NULL;

// Labels info
static lv_obj_t *g_money_label = NULL;
static lv_obj_t *g_health_label = NULL;
static lv_obj_t *g_mood_label = NULL;

// Boutons d'action
static lv_obj_t *g_btn_feed = NULL;
static lv_obj_t *g_btn_water = NULL;
static lv_obj_t *g_btn_heat = NULL;
static lv_obj_t *g_btn_mist = NULL;
static lv_obj_t *g_btn_clean = NULL;
static lv_obj_t *g_btn_play = NULL;
static lv_obj_t *g_btn_stats = NULL;
static lv_obj_t *g_btn_shop = NULL;

// Panel latéral boutons
static lv_obj_t *g_action_panel = NULL;

// ====================================================================================
// PROTOTYPES CALLBACKS
// ====================================================================================

static void btn_feed_cb(lv_event_t *e);
static void btn_water_cb(lv_event_t *e);
static void btn_heat_cb(lv_event_t *e);
static void btn_mist_cb(lv_event_t *e);
static void btn_clean_cb(lv_event_t *e);
static void btn_play_cb(lv_event_t *e);
static void btn_stats_cb(lv_event_t *e);
static void btn_shop_cb(lv_event_t *e);

// ====================================================================================
// UTILITAIRES
// ====================================================================================

static lv_color_t get_bar_color(uint8_t value) {
    if (value < CRITICAL_LEVEL) {
        return lv_color_hex(0xFF0000); // Rouge
    } else if (value < LOW_LEVEL) {
        return lv_color_hex(0xFF8800); // Orange
    } else if (value < GOOD_LEVEL) {
        return lv_color_hex(0xFFFF00); // Jaune
    } else {
        return lv_color_hex(0x00FF00); // Vert
    }
}

static lv_obj_t *create_need_bar(lv_obj_t *parent, const char *label_text, int y_pos) {
    // Label
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, label_text);
    lv_obj_set_pos(label, 20, y_pos);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    // Barre de progression
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 200, 20);
    lv_obj_set_pos(bar, 150, y_pos);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 50, LV_ANIM_OFF);

    return bar;
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, int x, int y) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 160, 60);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

// ====================================================================================
// INITIALISATION
// ====================================================================================

void ui_pet_init(lv_obj_t *parent_screen) {
    ESP_LOGI(TAG, "Initialisation interface Tribolonotus");

    g_main_screen = lv_obj_create(parent_screen);
    lv_obj_set_size(g_main_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_main_screen, lv_color_hex(0x1a1a2e), 0);

    // ===== ZONE PRINCIPALE : IMAGE + INFO LÉZARD =====

    // Cadre principal pour le lézard (zone centrale)
    lv_obj_t *pet_frame = lv_obj_create(g_main_screen);
    lv_obj_set_size(pet_frame, 600, 400);
    lv_obj_set_pos(pet_frame, 20, 20);
    lv_obj_set_style_bg_color(pet_frame, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(pet_frame, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(pet_frame, 3, 0);
    lv_obj_set_style_radius(pet_frame, 15, 0);

    // Image du lézard (placeholder)
    g_pet_image = lv_obj_create(pet_frame);
    lv_obj_set_size(g_pet_image, 200, 200);
    lv_obj_center(g_pet_image);
    lv_obj_set_style_bg_color(g_pet_image, lv_color_hex(0x4caf50), 0);
    lv_obj_set_style_radius(g_pet_image, 100, 0);

    // Icône lézard (texte pour l'instant)
    lv_obj_t *pet_emoji = lv_label_create(g_pet_image);
    lv_label_set_text(pet_emoji, LV_SYMBOL_IMAGE);
    lv_obj_center(pet_emoji);
    lv_obj_set_style_text_font(pet_emoji, &lv_font_montserrat_48, 0);

    // Nom du lézard
    g_pet_name_label = lv_label_create(pet_frame);
    lv_label_set_text(g_pet_name_label, "Ruby");
    lv_obj_set_pos(g_pet_name_label, 20, 20);
    lv_obj_set_style_text_font(g_pet_name_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_pet_name_label, lv_color_white(), 0);

    // Info espèce/âge/sexe
    g_pet_info_label = lv_label_create(pet_frame);
    lv_label_set_text(g_pet_info_label, "T. gracilis | Œuf | 0j");
    lv_obj_set_pos(g_pet_info_label, 20, 55);
    lv_obj_set_style_text_color(g_pet_info_label, lv_color_hex(0xaaaaaa), 0);

    // ===== ZONE BESOINS (barres) =====

    lv_obj_t *needs_panel = lv_obj_create(g_main_screen);
    lv_obj_set_size(needs_panel, 600, 180);
    lv_obj_set_pos(needs_panel, 20, 430);
    lv_obj_set_style_bg_color(needs_panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(needs_panel, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(needs_panel, 3, 0);
    lv_obj_set_style_radius(needs_panel, 15, 0);

    // Titre
    lv_obj_t *needs_title = lv_label_create(needs_panel);
    lv_label_set_text(needs_title, "BESOINS");
    lv_obj_set_pos(needs_title, 20, 10);
    lv_obj_set_style_text_font(needs_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(needs_title, lv_color_white(), 0);

    // Créer les barres
    g_hunger_bar = create_need_bar(needs_panel, LV_SYMBOL_HOME " Faim", 40);
    g_thirst_bar = create_need_bar(needs_panel, LV_SYMBOL_REFRESH " Soif", 70);
    g_temp_bar = create_need_bar(needs_panel, LV_SYMBOL_CHARGE " Temp", 100);

    g_humidity_bar = create_need_bar(needs_panel, LV_SYMBOL_SD_CARD " Humid", 40);
    lv_obj_set_pos(g_humidity_bar, 380, 40);
    lv_obj_set_pos(lv_obj_get_parent(g_humidity_bar), 0, 0); // Ajuster label

    g_clean_bar = create_need_bar(needs_panel, LV_SYMBOL_EJECT " Propr", 70);
    lv_obj_set_pos(g_clean_bar, 380, 70);

    g_happy_bar = create_need_bar(needs_panel, LV_SYMBOL_CALL " Bonheur", 100);
    lv_obj_set_pos(g_happy_bar, 380, 100);

    // Labels barres droite
    lv_obj_t *lbl_humid = lv_label_create(needs_panel);
    lv_label_set_text(lbl_humid, LV_SYMBOL_SD_CARD " Humid");
    lv_obj_set_pos(lbl_humid, 370, 40);
    lv_obj_set_style_text_color(lbl_humid, lv_color_white(), 0);

    lv_obj_t *lbl_clean = lv_label_create(needs_panel);
    lv_label_set_text(lbl_clean, LV_SYMBOL_EJECT " Propr");
    lv_obj_set_pos(lbl_clean, 370, 70);
    lv_obj_set_style_text_color(lbl_clean, lv_color_white(), 0);

    lv_obj_t *lbl_happy = lv_label_create(needs_panel);
    lv_label_set_text(lbl_happy, LV_SYMBOL_CALL " Bonheur");
    lv_obj_set_pos(lbl_happy, 370, 100);
    lv_obj_set_style_text_color(lbl_happy, lv_color_white(), 0);

    // ===== ZONE SANTÉ / ARGENT =====

    lv_obj_t *info_panel = lv_obj_create(needs_panel);
    lv_obj_set_size(info_panel, 560, 40);
    lv_obj_set_pos(info_panel, 20, 130);
    lv_obj_set_style_bg_color(info_panel, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(info_panel, 0, 0);

    g_health_label = lv_label_create(info_panel);
    lv_label_set_text(g_health_label, LV_SYMBOL_WARNING " Santé: Excellente");
    lv_obj_set_pos(g_health_label, 10, 10);
    lv_obj_set_style_text_color(g_health_label, lv_color_hex(0x00ff00), 0);

    g_mood_label = lv_label_create(info_panel);
    lv_label_set_text(g_mood_label, LV_SYMBOL_CALL " Humeur: Content");
    lv_obj_set_pos(g_mood_label, 220, 10);
    lv_obj_set_style_text_color(g_mood_label, lv_color_hex(0xffff00), 0);

    g_money_label = lv_label_create(info_panel);
    lv_label_set_text(g_money_label, LV_SYMBOL_DOWNLOAD " $500");
    lv_obj_set_pos(g_money_label, 430, 10);
    lv_obj_set_style_text_color(g_money_label, lv_color_hex(0xffd700), 0);

    // ===== PANEL D'ACTIONS (DROITE) =====

    g_action_panel = lv_obj_create(g_main_screen);
    lv_obj_set_size(g_action_panel, 380, 590);
    lv_obj_set_pos(g_action_panel, 630, 20);
    lv_obj_set_style_bg_color(g_action_panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(g_action_panel, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(g_action_panel, 3, 0);
    lv_obj_set_style_radius(g_action_panel, 15, 0);

    // Titre
    lv_obj_t *actions_title = lv_label_create(g_action_panel);
    lv_label_set_text(actions_title, "ACTIONS");
    lv_obj_set_pos(actions_title, 20, 15);
    lv_obj_set_style_text_font(actions_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(actions_title, lv_color_white(), 0);

    // Boutons d'action (2 colonnes)
    g_btn_feed = create_action_button(g_action_panel, LV_SYMBOL_HOME " Nourrir", btn_feed_cb, 20, 60);
    g_btn_water = create_action_button(g_action_panel, LV_SYMBOL_REFRESH " Abreuver", btn_water_cb, 200, 60);

    g_btn_heat = create_action_button(g_action_panel, LV_SYMBOL_CHARGE " Chauffer", btn_heat_cb, 20, 140);
    g_btn_mist = create_action_button(g_action_panel, LV_SYMBOL_SD_CARD " Brumiser", btn_mist_cb, 200, 140);

    g_btn_clean = create_action_button(g_action_panel, LV_SYMBOL_EJECT " Nettoyer", btn_clean_cb, 20, 220);
    g_btn_play = create_action_button(g_action_panel, LV_SYMBOL_CALL " Jouer", btn_play_cb, 200, 220);

    g_btn_stats = create_action_button(g_action_panel, LV_SYMBOL_LIST " Stats", btn_stats_cb, 20, 320);
    g_btn_shop = create_action_button(g_action_panel, LV_SYMBOL_DOWNLOAD " Boutique", btn_shop_cb, 200, 320);

    // Style boutons
    lv_obj_set_style_bg_color(g_btn_feed, lv_color_hex(0xe91e63), 0);
    lv_obj_set_style_bg_color(g_btn_water, lv_color_hex(0x2196f3), 0);
    lv_obj_set_style_bg_color(g_btn_heat, lv_color_hex(0xff5722), 0);
    lv_obj_set_style_bg_color(g_btn_mist, lv_color_hex(0x00bcd4), 0);
    lv_obj_set_style_bg_color(g_btn_clean, lv_color_hex(0x8bc34a), 0);
    lv_obj_set_style_bg_color(g_btn_play, lv_color_hex(0xffc107), 0);
    lv_obj_set_style_bg_color(g_btn_stats, lv_color_hex(0x9c27b0), 0);
    lv_obj_set_style_bg_color(g_btn_shop, lv_color_hex(0x4caf50), 0);

    ESP_LOGI(TAG, "Interface initialisée");
}

// ====================================================================================
// MISE À JOUR
// ====================================================================================

void ui_pet_update(void) {
    pet_t *pet = pet_get_current();

    if (!pet || !pet->is_alive) {
        lv_label_set_text(g_pet_name_label, "Aucun lézard actif");
        return;
    }

    // Mettre à jour nom
    lv_label_set_text(g_pet_name_label, pet->name);

    // Info espèce
    const species_info_t *info = pet_get_species_info(pet->species);
    char info_buf[128];
    const char *sex_str = (pet->sex == SEX_MALE) ? "Mâle" : (pet->sex == SEX_FEMALE) ? "Femelle" : "?";
    snprintf(info_buf, sizeof(info_buf), "%s | %s | %s | %luj",
             info->name_common,
             pet_stage_to_string(pet->stage),
             sex_str,
             pet->stats.age_days);
    lv_label_set_text(g_pet_info_label, info_buf);

    // Mettre à jour barres (ATTENTION: faim/soif sont inversées, 100 = affamé)
    lv_bar_set_value(g_hunger_bar, 100 - pet->needs.hunger, LV_ANIM_ON);
    lv_bar_set_value(g_thirst_bar, 100 - pet->needs.thirst, LV_ANIM_ON);
    lv_bar_set_value(g_temp_bar, pet->needs.temperature, LV_ANIM_ON);
    lv_bar_set_value(g_humidity_bar, pet->needs.humidity, LV_ANIM_ON);
    lv_bar_set_value(g_clean_bar, pet->needs.cleanliness, LV_ANIM_ON);
    lv_bar_set_value(g_happy_bar, pet->needs.happiness, LV_ANIM_ON);

    // Couleurs des barres
    lv_obj_set_style_bg_color(g_hunger_bar, get_bar_color(100 - pet->needs.hunger), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_thirst_bar, get_bar_color(100 - pet->needs.thirst), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_temp_bar, get_bar_color(pet->needs.temperature), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_humidity_bar, get_bar_color(pet->needs.humidity), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_clean_bar, get_bar_color(pet->needs.cleanliness), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_happy_bar, get_bar_color(pet->needs.happiness), LV_PART_INDICATOR);

    // Santé et humeur
    char health_buf[64];
    snprintf(health_buf, sizeof(health_buf), LV_SYMBOL_WARNING " Santé: %s", pet_health_to_string(pet->health.status));
    lv_label_set_text(g_health_label, health_buf);

    char mood_buf[64];
    snprintf(mood_buf, sizeof(mood_buf), LV_SYMBOL_CALL " Humeur: %s", pet_mood_to_string(pet->mood));
    lv_label_set_text(g_mood_label, mood_buf);

    // Argent
    char money_buf[32];
    snprintf(money_buf, sizeof(money_buf), LV_SYMBOL_DOWNLOAD " $%ld", pet_get_money());
    lv_label_set_text(g_money_label, money_buf);

    // Couleur santé
    lv_color_t health_color;
    if (pet->health.status >= HEALTH_GOOD) {
        health_color = lv_color_hex(0x00ff00);
    } else if (pet->health.status >= HEALTH_WEAK) {
        health_color = lv_color_hex(0xffff00);
    } else {
        health_color = lv_color_hex(0xff0000);
    }
    lv_obj_set_style_text_color(g_health_label, health_color, 0);
}

// ====================================================================================
// CALLBACKS BOUTONS
// ====================================================================================

static void btn_feed_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton NOURRIR");

    pet_t *pet = pet_get_current();
    if (!pet) {
        ui_pet_show_message("Erreur", "Aucun lézard sélectionné");
        return;
    }

    // Pour l'instant, nourriture par défaut = grillons
    if (pet_feed(g_game_state.current_pet_index, FOOD_CRICKET)) {
        ui_pet_show_message("Succès", "Votre lézard a mangé !");
    } else {
        ui_pet_show_message("Erreur", "Plus de grillons !\nAllez à la boutique.");
    }

    ui_pet_update();
}

static void btn_water_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton ABREUVER");

    if (pet_water(g_game_state.current_pet_index)) {
        ui_pet_show_message("Succès", "Lézard hydraté !");
    }
    ui_pet_update();
}

static void btn_heat_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton CHAUFFER");

    if (pet_heat(g_game_state.current_pet_index, 10)) { // 10 minutes
        ui_pet_show_message("Succès", "Zone chaude activée !");
    }
    ui_pet_update();
}

static void btn_mist_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton BRUMISER");

    if (pet_mist(g_game_state.current_pet_index)) {
        ui_pet_show_message("Succès", "Terrarium brumisé !");
    }
    ui_pet_update();
}

static void btn_clean_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton NETTOYER");

    if (pet_clean(g_game_state.current_pet_index)) {
        ui_pet_show_message("Succès", "Terrarium nettoyé !");
    }
    ui_pet_update();
}

static void btn_play_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton JOUER");

    if (pet_play(g_game_state.current_pet_index)) {
        ui_pet_show_message("Succès", "Votre lézard est content !");
    }
    ui_pet_update();
}

static void btn_stats_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton STATS");
    ui_pet_show_stats();
}

static void btn_shop_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton BOUTIQUE");
    ui_pet_show_shop();
}

// ====================================================================================
// ÉCRANS ADDITIONNELS
// ====================================================================================

void ui_pet_show_main_screen(void) {
    // Déjà affiché par défaut
    ESP_LOGI(TAG, "Écran principal");
}

void ui_pet_show_pet_list(void) {
    ESP_LOGI(TAG, "Liste des lézards (TODO)");
}

void ui_pet_show_actions_menu(void) {
    ESP_LOGI(TAG, "Menu actions (TODO)");
}

void ui_pet_show_stats(void) {
    pet_t *pet = pet_get_current();
    if (!pet) {
        return;
    }

    char stats_buf[512];
    snprintf(stats_buf, sizeof(stats_buf),
             "=== STATISTIQUES ===\n\n"
             "Nom: %s\n"
             "Espèce: %s\n"
             "Âge: %lu jours\n"
             "Stade: %s\n"
             "Sexe: %s\n"
             "Poids: %u g\n"
             "Longueur: %u mm\n\n"
             "Repas totaux: %lu\n"
             "Interactions: %lu\n"
             "Descendants: %u\n",
             pet->name,
             pet_get_species_info(pet->species)->name_common,
             pet->stats.age_days,
             pet_stage_to_string(pet->stage),
             pet->sex == SEX_MALE ? "Mâle" : pet->sex == SEX_FEMALE ? "Femelle" : "Inconnu",
             pet->stats.weight_grams,
             pet->stats.length_mm,
             pet->stats.total_feeds,
             pet->stats.total_interactions,
             pet->stats.offspring_count);

    ui_pet_show_message("Statistiques", stats_buf);
}

void ui_pet_show_shop(void) {
    inventory_t *inv = pet_get_inventory();

    char shop_buf[256];
    snprintf(shop_buf, sizeof(shop_buf),
             "=== INVENTAIRE ===\n\n"
             "Grillons: %u ($1/u)\n"
             "Dubias: %u ($2/u)\n"
             "Vers farine: %u ($3/u)\n"
             "Cloportes: %u ($2/u)\n"
             "Vers terre: %u ($2/u)\n\n"
             "Argent: $%ld\n\n"
             "Boutique complète à venir !",
             inv->crickets,
             inv->dubias,
             inv->waxworms,
             inv->isopods,
             inv->earthworms,
             pet_get_money());

    ui_pet_show_message("Boutique", shop_buf);
}

void ui_pet_show_new_pet_menu(void) {
    ESP_LOGI(TAG, "Menu nouveau lézard (TODO)");
}

void ui_pet_show_message(const char *title, const char *message) {
    // Message box simple
    lv_obj_t *mbox = lv_msgbox_create(NULL, title, message, NULL, true);
    lv_obj_center(mbox);

    ESP_LOGI(TAG, "Message: %s - %s", title, message);
}
