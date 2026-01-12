/**
 * @file ui_pet_enhanced.c
 * @brief Version améliorée de l'interface avec menus interactifs complets
 */

#include "ui_pet.h"
#include "pet_simulator.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>

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
static lv_obj_t *g_btn_pets = NULL;
static lv_obj_t *g_btn_new_pet = NULL;

// Panel latéral boutons
static lv_obj_t *g_action_panel = NULL;

// Menus contextuels
static lv_obj_t *g_food_menu = NULL;
static lv_obj_t *g_shop_menu = NULL;
static lv_obj_t *g_pet_list_menu = NULL;
static lv_obj_t *g_new_pet_menu = NULL;

// Alertes
static lv_obj_t *g_alert_label = NULL;
static uint32_t g_last_alert_check = 0;

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
static void btn_pets_cb(lv_event_t *e);
static void btn_new_pet_cb(lv_event_t *e);

static void food_menu_cb(lv_event_t *e);
static void shop_buy_cb(lv_event_t *e);
static void pet_select_cb(lv_event_t *e);
static void new_pet_create_cb(lv_event_t *e);
static void close_menu_cb(lv_event_t *e);

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

static lv_color_t get_species_color(tribolonotus_species_t species) {
    static const uint32_t colors[] = {
        0x4caf50, // T. gracilis - Vert
        0x2196f3, // T. novaeguineae - Bleu
        0x9c27b0, // T. ponceleti - Violet
        0xff5722, // T. psychosauropus - Orange
        0xe91e63, // T. pseudoponceleti - Rose
        0x00bcd4, // T. brongersmai - Cyan
        0x8bc34a, // T. annectens - Vert clair
        0xff9800, // T. parkeri - Orange foncé
        0x673ab7, // T. blanchardi - Violet foncé
        0x009688  // T. schmidti - Teal
    };

    if (species < SPECIES_COUNT) {
        return lv_color_hex(colors[species]);
    }
    return lv_color_hex(0x808080);
}

static lv_obj_t *create_need_bar(lv_obj_t *parent, const char *label_text, int x, int y) {
    // Label
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, label_text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    // Barre de progression
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 180, 18);
    lv_obj_set_pos(bar, x + 100, y - 2);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 50, LV_ANIM_OFF);

    return bar;
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, int x, int y, lv_color_t color) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 160, 55);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn, color, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

// ====================================================================================
// INITIALISATION
// ====================================================================================

void ui_pet_init(lv_obj_t *parent_screen) {
    ESP_LOGI(TAG, "Initialisation interface Tribolonotus Enhanced");

    g_main_screen = lv_obj_create(parent_screen);
    lv_obj_set_size(g_main_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_main_screen, lv_color_hex(0x0a0a0a), 0);

    // ===== CADRE PRINCIPAL LÉZARD =====
    lv_obj_t *pet_frame = lv_obj_create(g_main_screen);
    lv_obj_set_size(pet_frame, 580, 350);
    lv_obj_set_pos(pet_frame, 15, 15);
    lv_obj_set_style_bg_color(pet_frame, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(pet_frame, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(pet_frame, 3, 0);
    lv_obj_set_style_radius(pet_frame, 10, 0);

    // Image du lézard
    g_pet_image = lv_obj_create(pet_frame);
    lv_obj_set_size(g_pet_image, 180, 180);
    lv_obj_set_pos(g_pet_image, 200, 80);
    lv_obj_set_style_bg_color(g_pet_image, lv_color_hex(0x4caf50), 0);
    lv_obj_set_style_radius(g_pet_image, 90, 0);

    lv_obj_t *pet_emoji = lv_label_create(g_pet_image);
    lv_label_set_text(pet_emoji, "🦎");
    lv_obj_center(pet_emoji);
    lv_obj_set_style_text_font(pet_emoji, &lv_font_montserrat_48, 0);

    // Nom du lézard
    g_pet_name_label = lv_label_create(pet_frame);
    lv_label_set_text(g_pet_name_label, "Ruby");
    lv_obj_set_pos(g_pet_name_label, 15, 15);
    lv_obj_set_style_text_font(g_pet_name_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_pet_name_label, lv_color_white(), 0);

    // Info espèce
    g_pet_info_label = lv_label_create(pet_frame);
    lv_label_set_text(g_pet_info_label, "T. gracilis | Œuf | 0j");
    lv_obj_set_pos(g_pet_info_label, 15, 48);
    lv_obj_set_style_text_color(g_pet_info_label, lv_color_hex(0xaaaaaa), 0);

    // Santé/Humeur (haut)
    g_health_label = lv_label_create(pet_frame);
    lv_label_set_text(g_health_label, LV_SYMBOL_WARNING " Excellente");
    lv_obj_set_pos(g_health_label, 15, 280);
    lv_obj_set_style_text_color(g_health_label, lv_color_hex(0x00ff00), 0);

    g_mood_label = lv_label_create(pet_frame);
    lv_label_set_text(g_mood_label, LV_SYMBOL_CALL " Content");
    lv_obj_set_pos(g_mood_label, 200, 280);
    lv_obj_set_style_text_color(g_mood_label, lv_color_hex(0xffff00), 0);

    g_money_label = lv_label_create(pet_frame);
    lv_label_set_text(g_money_label, LV_SYMBOL_DOWNLOAD " $500");
    lv_obj_set_pos(g_money_label, 450, 280);
    lv_obj_set_style_text_color(g_money_label, lv_color_hex(0xffd700), 0);

    // Alerte (en rouge si critique)
    g_alert_label = lv_label_create(pet_frame);
    lv_label_set_text(g_alert_label, "");
    lv_obj_set_pos(g_alert_label, 15, 310);
    lv_obj_set_style_text_color(g_alert_label, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_text_font(g_alert_label, &lv_font_montserrat_16, 0);

    // ===== PANEL BESOINS =====
    lv_obj_t *needs_panel = lv_obj_create(g_main_screen);
    lv_obj_set_size(needs_panel, 580, 220);
    lv_obj_set_pos(needs_panel, 15, 375);
    lv_obj_set_style_bg_color(needs_panel, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(needs_panel, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(needs_panel, 3, 0);
    lv_obj_set_style_radius(needs_panel, 10, 0);

    lv_obj_t *needs_title = lv_label_create(needs_panel);
    lv_label_set_text(needs_title, "BESOINS VITAUX");
    lv_obj_set_pos(needs_title, 15, 10);
    lv_obj_set_style_text_font(needs_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(needs_title, lv_color_white(), 0);

    // Barres (2 colonnes)
    g_hunger_bar = create_need_bar(needs_panel, "🍖 Faim", 15, 45);
    g_thirst_bar = create_need_bar(needs_panel, "💧 Soif", 15, 80);
    g_temp_bar = create_need_bar(needs_panel, "🌡️ Temp", 15, 115);

    g_humidity_bar = create_need_bar(needs_panel, "💨 Humid", 300, 45);
    g_clean_bar = create_need_bar(needs_panel, "🧹 Propr", 300, 80);
    g_happy_bar = create_need_bar(needs_panel, "😊 Bonheur", 300, 115);

    // Stats rapides
    lv_obj_t *stats_label = lv_label_create(needs_panel);
    lv_label_set_text(stats_label, "Poids: 0g | Longueur: 0mm | Repas: 0 | Jours: 0");
    lv_obj_set_pos(stats_label, 15, 155);
    lv_obj_set_style_text_color(stats_label, lv_color_hex(0x888888), 0);

    // ===== PANEL D'ACTIONS (DROITE) =====
    g_action_panel = lv_obj_create(g_main_screen);
    lv_obj_set_size(g_action_panel, 410, 580);
    lv_obj_set_pos(g_action_panel, 600, 15);
    lv_obj_set_style_bg_color(g_action_panel, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(g_action_panel, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(g_action_panel, 3, 0);
    lv_obj_set_style_radius(g_action_panel, 10, 0);

    lv_obj_t *actions_title = lv_label_create(g_action_panel);
    lv_label_set_text(actions_title, "ACTIONS");
    lv_obj_set_pos(actions_title, 15, 12);
    lv_obj_set_style_text_font(actions_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(actions_title, lv_color_white(), 0);

    // Boutons d'action (2 colonnes, 5 lignes)
    g_btn_feed = create_action_button(g_action_panel, "🍖 Nourrir", btn_feed_cb, 15, 50, lv_color_hex(0xe91e63));
    g_btn_water = create_action_button(g_action_panel, "💧 Abreuver", btn_water_cb, 185, 50, lv_color_hex(0x2196f3));

    g_btn_heat = create_action_button(g_action_panel, "🌡️ Chauffer", btn_heat_cb, 15, 115, lv_color_hex(0xff5722));
    g_btn_mist = create_action_button(g_action_panel, "💨 Brumiser", btn_mist_cb, 185, 115, lv_color_hex(0x00bcd4));

    g_btn_clean = create_action_button(g_action_panel, "🧹 Nettoyer", btn_clean_cb, 15, 180, lv_color_hex(0x8bc34a));
    g_btn_play = create_action_button(g_action_panel, "😊 Jouer", btn_play_cb, 185, 180, lv_color_hex(0xffc107));

    g_btn_stats = create_action_button(g_action_panel, "📊 Stats", btn_stats_cb, 15, 260, lv_color_hex(0x9c27b0));
    g_btn_shop = create_action_button(g_action_panel, "🛒 Boutique", btn_shop_cb, 185, 260, lv_color_hex(0x4caf50));

    g_btn_pets = create_action_button(g_action_panel, "🦎 Mes lézards", btn_pets_cb, 15, 340, lv_color_hex(0x673ab7));
    g_btn_new_pet = create_action_button(g_action_panel, "➕ Nouveau", btn_new_pet_cb, 185, 340, lv_color_hex(0x3f51b5));

    // Bouton Sauvegarder
    lv_obj_t *btn_save = create_action_button(g_action_panel, "💾 Sauvegarder", NULL, 15, 420, lv_color_hex(0x607d8b));
    lv_obj_add_event_cb(btn_save, close_menu_cb, LV_EVENT_CLICKED, NULL); // Réutilise callback

    ESP_LOGI(TAG, "Interface initialisée");
}

// ====================================================================================
// MISE À JOUR
// ====================================================================================

void ui_pet_update(void) {
    pet_t *pet = pet_get_current();

    if (!pet || !pet->is_alive) {
        lv_label_set_text(g_pet_name_label, "❌ Aucun lézard actif");
        lv_label_set_text(g_alert_label, "Créez un nouveau lézard !");
        return;
    }

    // Mettre à jour couleur selon espèce
    lv_obj_set_style_bg_color(g_pet_image, get_species_color(pet->species), 0);

    // Nom
    lv_label_set_text(g_pet_name_label, pet->name);

    // Info
    const species_info_t *info = pet_get_species_info(pet->species);
    char info_buf[128];
    const char *sex_str = (pet->sex == SEX_MALE) ? "♂️" : (pet->sex == SEX_FEMALE) ? "♀️" : "?";
    snprintf(info_buf, sizeof(info_buf), "%s | %s %s | %lu j",
             info->name_common,
             pet_stage_to_string(pet->stage),
             sex_str,
             pet->stats.age_days);
    lv_label_set_text(g_pet_info_label, info_buf);

    // Barres (inversé pour faim/soif)
    lv_bar_set_value(g_hunger_bar, 100 - pet->needs.hunger, LV_ANIM_ON);
    lv_bar_set_value(g_thirst_bar, 100 - pet->needs.thirst, LV_ANIM_ON);
    lv_bar_set_value(g_temp_bar, pet->needs.temperature, LV_ANIM_ON);
    lv_bar_set_value(g_humidity_bar, pet->needs.humidity, LV_ANIM_ON);
    lv_bar_set_value(g_clean_bar, pet->needs.cleanliness, LV_ANIM_ON);
    lv_bar_set_value(g_happy_bar, pet->needs.happiness, LV_ANIM_ON);

    // Couleurs
    lv_obj_set_style_bg_color(g_hunger_bar, get_bar_color(100 - pet->needs.hunger), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_thirst_bar, get_bar_color(100 - pet->needs.thirst), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_temp_bar, get_bar_color(pet->needs.temperature), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_humidity_bar, get_bar_color(pet->needs.humidity), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_clean_bar, get_bar_color(pet->needs.cleanliness), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_happy_bar, get_bar_color(pet->needs.happiness), LV_PART_INDICATOR);

    // Santé
    char health_buf[64];
    snprintf(health_buf, sizeof(health_buf), LV_SYMBOL_WARNING " %s", pet_health_to_string(pet->health.status));
    lv_label_set_text(g_health_label, health_buf);

    lv_color_t health_color = (pet->health.status >= HEALTH_GOOD) ? lv_color_hex(0x00ff00) :
                              (pet->health.status >= HEALTH_WEAK) ? lv_color_hex(0xffff00) :
                              lv_color_hex(0xff0000);
    lv_obj_set_style_text_color(g_health_label, health_color, 0);

    // Humeur
    char mood_buf[64];
    snprintf(mood_buf, sizeof(mood_buf), LV_SYMBOL_CALL " %s", pet_mood_to_string(pet->mood));
    lv_label_set_text(g_mood_label, mood_buf);

    // Argent
    char money_buf[32];
    snprintf(money_buf, sizeof(money_buf), LV_SYMBOL_DOWNLOAD " $%lu", pet_get_money());
    lv_label_set_text(g_money_label, money_buf);

    // Alertes critiques (toutes les 5 secondes)
    uint32_t now = lv_tick_get();
    if (now - g_last_alert_check > 5000) {
        g_last_alert_check = now;

        if (pet->needs.hunger > 80 && pet->needs.thirst > 80) {
            lv_label_set_text(g_alert_label, "⚠️ URGENT: FAIM ET SOIF CRITIQUE !");
        } else if (pet->needs.hunger > 80) {
            lv_label_set_text(g_alert_label, "⚠️ Votre lézard a faim !");
        } else if (pet->needs.thirst > 80) {
            lv_label_set_text(g_alert_label, "⚠️ Votre lézard a soif !");
        } else if (pet->needs.temperature < 30) {
            lv_label_set_text(g_alert_label, "⚠️ Température trop basse !");
        } else if (pet->health.status <= HEALTH_WEAK) {
            lv_label_set_text(g_alert_label, "⚠️ Santé faible, consultez un vétérinaire !");
        } else {
            lv_label_set_text(g_alert_label, "");
        }
    }
}

// ====================================================================================
// MENU SÉLECTION NOURRITURE
// ====================================================================================

static void show_food_menu(void) {
    if (g_food_menu) {
        lv_obj_del(g_food_menu);
    }

    g_food_menu = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_food_menu, 500, 400);
    lv_obj_center(g_food_menu);
    lv_obj_set_style_bg_color(g_food_menu, lv_color_hex(0x2a2a3e), 0);

    lv_obj_t *title = lv_label_create(g_food_menu);
    lv_label_set_text(title, "🍖 CHOISIR NOURRITURE");
    lv_obj_set_pos(title, 20, 15);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    inventory_t *inv = pet_get_inventory();

    const char *food_names[] = {"Grillons", "Dubias", "Vers farine", "Cloportes", "Vers terre"};
    uint16_t *food_counts[] = {&inv->crickets, &inv->dubias, &inv->waxworms, &inv->isopods, &inv->earthworms};

    for (int i = 0; i < FOOD_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(g_food_menu);
        lv_obj_set_size(btn, 220, 50);
        lv_obj_set_pos(btn, 20 + (i % 2) * 240, 60 + (i / 2) * 65);

        char label_buf[64];
        snprintf(label_buf, sizeof(label_buf), "%s (%u)", food_names[i], *food_counts[i]);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, label_buf);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, food_menu_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        if (*food_counts[i] == 0) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
    }

    // Bouton fermer
    lv_obj_t *btn_close = lv_btn_create(g_food_menu);
    lv_obj_set_size(btn_close, 460, 50);
    lv_obj_set_pos(btn_close, 20, 320);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x666666), 0);

    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Annuler");
    lv_obj_center(lbl_close);

    lv_obj_add_event_cb(btn_close, close_menu_cb, LV_EVENT_CLICKED, NULL);
}

static void food_menu_cb(lv_event_t *e) {
    food_type_t food = (food_type_t)(intptr_t)lv_event_get_user_data(e);

    if (pet_feed(pet_get_current_index(), food)) {
        ESP_LOGI(TAG, "Lézard nourri avec succès");
        ui_pet_show_message("Succès", "Votre lézard a mangé !");
    } else {
        ui_pet_show_message("Erreur", "Plus de cette nourriture !");
    }

    if (g_food_menu) {
        lv_obj_del(g_food_menu);
        g_food_menu = NULL;
    }

    ui_pet_update();
}

// ====================================================================================
// CALLBACKS BOUTONS
// ====================================================================================

static void btn_feed_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton NOURRIR");
    show_food_menu();
}

static void btn_water_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Bouton ABREUVER");

    pet_t *pet = pet_get_current();
    if (pet && pet_water(pet_get_current_index())) {
        ui_pet_show_message("Succès", "Lézard hydraté ! 💧");
    }
    ui_pet_update();
}

static void btn_heat_cb(lv_event_t *e) {
    if (pet_heat(pet_get_current_index(), 10)) {
        ui_pet_show_message("Succès", "Zone chaude activée ! 🌡️");
    }
    ui_pet_update();
}

static void btn_mist_cb(lv_event_t *e) {
    if (pet_mist(pet_get_current_index())) {
        ui_pet_show_message("Succès", "Terrarium brumisé ! 💨");
    }
    ui_pet_update();
}

static void btn_clean_cb(lv_event_t *e) {
    if (pet_clean(pet_get_current_index())) {
        ui_pet_show_message("Succès", "Terrarium nettoyé ! 🧹");
    }
    ui_pet_update();
}

static void btn_play_cb(lv_event_t *e) {
    if (pet_play(pet_get_current_index())) {
        ui_pet_show_message("Succès", "Votre lézard est content ! 😊");
    }
    ui_pet_update();
}

static void btn_stats_cb(lv_event_t *e) {
    ui_pet_show_stats();
}

static void btn_shop_cb(lv_event_t *e) {
    ui_pet_show_shop();
}

static void btn_pets_cb(lv_event_t *e) {
    ui_pet_show_pet_list();
}

static void btn_new_pet_cb(lv_event_t *e) {
    ui_pet_show_new_pet_menu();
}

static void close_menu_cb(lv_event_t *e) {
    lv_obj_t *menu = lv_event_get_target(e);
    lv_obj_t *parent = lv_obj_get_parent(menu);

    if (g_food_menu && (parent == g_food_menu || menu == g_food_menu)) {
        lv_obj_del(g_food_menu);
        g_food_menu = NULL;
    } else if (g_shop_menu && (parent == g_shop_menu || menu == g_shop_menu)) {
        lv_obj_del(g_shop_menu);
        g_shop_menu = NULL;
    } else if (g_pet_list_menu && (parent == g_pet_list_menu || menu == g_pet_list_menu)) {
        lv_obj_del(g_pet_list_menu);
        g_pet_list_menu = NULL;
    } else if (g_new_pet_menu && (parent == g_new_pet_menu || menu == g_new_pet_menu)) {
        lv_obj_del(g_new_pet_menu);
        g_new_pet_menu = NULL;
    } else {
        // Sauvegarder si c'est le bouton save
        pet_simulator_save();
        ui_pet_show_message("Sauvegarde", "Partie sauvegardée ! 💾");
    }
}

static void shop_buy_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    intptr_t item_id = (intptr_t)lv_event_get_user_data(e);

    // Fermer le menu boutique
    if (g_shop_menu) {
        lv_obj_del(g_shop_menu);
        g_shop_menu = NULL;
    }

    // TODO: Implémenter l'achat d'items
    ESP_LOGI(TAG, "Achat item %d", (int)item_id);
    ui_pet_show_message("Boutique", "Fonctionnalité d'achat en développement");
}

static void pet_select_cb(lv_event_t *e) {
    intptr_t pet_index = (intptr_t)lv_event_get_user_data(e);

    // Fermer le menu liste
    if (g_pet_list_menu) {
        lv_obj_del(g_pet_list_menu);
        g_pet_list_menu = NULL;
    }

    // Changer le pet courant
    if (pet_index >= 0 && pet_index < MAX_PETS) {
        pet_set_current((int)pet_index);
        ui_pet_update();
        ESP_LOGI(TAG, "Pet sélectionné: %d", (int)pet_index);
    }
}

static void new_pet_create_cb(lv_event_t *e) {
    intptr_t species_id = (intptr_t)lv_event_get_user_data(e);

    // Fermer le menu nouveau pet
    if (g_new_pet_menu) {
        lv_obj_del(g_new_pet_menu);
        g_new_pet_menu = NULL;
    }

    // TODO: Créer un nouveau pet de l'espèce sélectionnée
    ESP_LOGI(TAG, "Création pet espèce %d", (int)species_id);
    ui_pet_show_message("Nouveau lézard", "Création en développement");
}

// ====================================================================================
// ÉCRANS ADDITIONNELS (implémentation simplifiée)
// ====================================================================================

void ui_pet_show_main_screen(void) {
    ESP_LOGI(TAG, "Écran principal");
}

void ui_pet_show_pet_list(void) {
    ESP_LOGI(TAG, "Liste lézards (TODO implémentation complète)");
    ui_pet_show_message("Mes lézards", "Fonctionnalité en développement");
}

void ui_pet_show_actions_menu(void) {
    ESP_LOGI(TAG, "Menu actions");
}

void ui_pet_show_stats(void) {
    pet_t *pet = pet_get_current();
    if (!pet) return;

    char stats_buf[512];
    const species_info_t *info = pet_get_species_info(pet->species);

    snprintf(stats_buf, sizeof(stats_buf),
             "📊 STATISTIQUES DÉTAILLÉES\n\n"
             "Nom: %s\n"
             "Espèce: %s\n"
             "Nom latin: %s\n"
             "Âge: %lu jours (%d ans)\n"
             "Stade: %s\n"
             "Sexe: %s\n\n"
             "📏 PHYSIQUE\n"
             "Poids: %u g\n"
             "Longueur: %u mm (adulte: %u mm)\n"
             "Variante couleur: #%02X\n\n"
             "🍖 ALIMENTATION\n"
             "Repas totaux: %lu\n"
             "Interactions: %lu\n"
             "Descendants: %u\n\n"
             "🏆 RARETÉ: %u/10",
             pet->name,
             info->name_common,
             info->name_latin,
             pet->stats.age_days,
             pet->stats.age_days / 365,
             pet_stage_to_string(pet->stage),
             pet->sex == SEX_MALE ? "Mâle ♂️" : pet->sex == SEX_FEMALE ? "Femelle ♀️" : "Inconnu",
             pet->stats.weight_grams,
             pet->stats.length_mm,
             info->adult_size_mm,
             pet->color_variant,
             pet->stats.total_feeds,
             pet->stats.total_interactions,
             pet->stats.offspring_count,
             info->rarity);

    ui_pet_show_message("Statistiques", stats_buf);
}

void ui_pet_show_shop(void) {
    inventory_t *inv = pet_get_inventory();

    char shop_buf[512];
    snprintf(shop_buf, sizeof(shop_buf),
             "🛒 BOUTIQUE & INVENTAIRE\n\n"
             "💰 Argent: $%lu\n\n"
             "📦 STOCK ACTUEL:\n"
             "Grillons: %u ($1/u)\n"
             "Dubias: %u ($2/u)\n"
             "Vers farine: %u ($3/u)\n"
             "Cloportes: %u ($2/u)\n"
             "Vers terre: %u ($2/u)\n\n"
             "💊 SOINS:\n"
             "Vitamines: %u\n"
             "Calcium: %u\n"
             "Médicaments: %u\n\n"
             "Boutique d'achat à venir !",
             pet_get_money(),
             inv->crickets,
             inv->dubias,
             inv->waxworms,
             inv->isopods,
             inv->earthworms,
             inv->vitamin_powder,
             inv->calcium_powder,
             inv->medications);

    ui_pet_show_message("Boutique", shop_buf);
}

void ui_pet_show_new_pet_menu(void) {
    ui_pet_show_message("Nouveau lézard", "Création en développement.\nPour l'instant, vous commencez avec Ruby (T. gracilis)");
}

void ui_pet_show_message(const char *title, const char *message) {
    lv_obj_t *mbox = lv_msgbox_create(NULL, title, message, NULL, true);
    lv_obj_center(mbox);
    lv_obj_set_style_bg_color(mbox, lv_color_hex(0x2a2a3e), 0);

    ESP_LOGI(TAG, "Message: %s - %s", title, message);
}
