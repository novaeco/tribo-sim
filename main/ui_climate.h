/**
 * @file ui_climate.h
 * @brief Climate Control UI Components for LVGL
 * @version 1.0
 * @date 2026-01-06
 *
 * Interface utilisateur LVGL pour la gestion climatique des terrariums.
 * Thème bleu/cyan pour différencier de la gestion des animaux (vert).
 */

#ifndef UI_CLIMATE_H
#define UI_CLIMATE_H

#include "climate_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================================
// THÈME COULEURS - CLIMAT (Bleu/Cyan)
// ====================================================================================

#define COLOR_CLIMATE_BG_DARK lv_color_hex(0x0A1520)   // Deep ocean blue
#define COLOR_CLIMATE_BG_CARD lv_color_hex(0x162035)   // Dark blue card
#define COLOR_CLIMATE_PRIMARY lv_color_hex(0x00B8D4)   // Cyan primary
#define COLOR_CLIMATE_SECONDARY lv_color_hex(0x40C4FF) // Light blue
#define COLOR_CLIMATE_ACCENT lv_color_hex(0x0097A7)    // Teal accent

// Température colors
#define COLOR_TEMP_HOT lv_color_hex(0xFF5722)  // Orange-red pour chaud
#define COLOR_TEMP_COLD lv_color_hex(0x2196F3) // Blue pour froid
#define COLOR_TEMP_GOOD lv_color_hex(0x4CAF50) // Green si OK

// Humidité
#define COLOR_HUMIDITY lv_color_hex(0x03A9F4) // Light blue

// UV colors par zone Ferguson
#define COLOR_UV_ZONE_1 lv_color_hex(0x9C27B0) // Purple - low UV
#define COLOR_UV_ZONE_2 lv_color_hex(0x3F51B5) // Indigo
#define COLOR_UV_ZONE_3 lv_color_hex(0xFFC107) // Amber
#define COLOR_UV_ZONE_4 lv_color_hex(0xFF9800) // Orange - high UV

// Alertes
#define COLOR_ALERT_WARNING lv_color_hex(0xFFC107)  // Amber
#define COLOR_ALERT_CRITICAL lv_color_hex(0xF44336) // Red
#define COLOR_ALERT_OK lv_color_hex(0x4CAF50)       // Green

// Équipements ON/OFF
#define COLOR_EQUIP_ON lv_color_hex(0x00E676)    // Green
#define COLOR_EQUIP_OFF lv_color_hex(0x607D8B)   // Grey
#define COLOR_EQUIP_ERROR lv_color_hex(0xF44336) // Red

// ====================================================================================
// PAGES PRINCIPALES
// ====================================================================================

/**
 * @brief Créer le dashboard climatique principal
 * @param parent Parent LVGL (écran principal)
 * @return Pointeur vers la page créée
 */
lv_obj_t *ui_climate_create_dashboard(lv_obj_t *parent);

/**
 * @brief Créer la page de détail d'un terrarium
 * @param parent Parent LVGL
 * @return Pointeur vers la page créée
 */
lv_obj_t *ui_climate_create_terrarium_detail(lv_obj_t *parent);

/**
 * @brief Créer la page de programmation horaire
 * @param parent Parent LVGL
 * @return Pointeur vers la page créée
 */
lv_obj_t *ui_climate_create_schedule_page(lv_obj_t *parent);

/**
 * @brief Créer la page des zones Ferguson / UV
 * @param parent Parent LVGL
 * @return Pointeur vers la page créée
 */
lv_obj_t *ui_climate_create_ferguson_page(lv_obj_t *parent);

/**
 * @brief Créer la page de gestion de l'eau
 * @param parent Parent LVGL
 * @return Pointeur vers la page créée
 */
lv_obj_t *ui_climate_create_water_page(lv_obj_t *parent);

/**
 * @brief Créer la page des alertes
 * @param parent Parent LVGL
 * @return Pointeur vers la page créée
 */
lv_obj_t *ui_climate_create_alerts_page(lv_obj_t *parent);

// ====================================================================================
// WIDGETS RÉUTILISABLES
// ====================================================================================

/**
 * @brief Créer une card de terrarium pour le dashboard
 * @param parent Parent (dashboard)
 * @param terrarium Configuration du terrarium
 * @return Pointeur vers la card
 */
lv_obj_t *ui_climate_create_terrarium_card(lv_obj_t *parent,
                                           const terrarium_config_t *terrarium);

/**
 * @brief Créer un widget de température (arc + valeur)
 * @param parent Parent
 * @param label_text Texte du label ("Zone Chaude", "Zone Froide")
 * @param is_hot_zone true pour zone chaude (rouge), false pour zone froide
 * (bleu)
 * @return Pointeur vers le container du widget
 */
lv_obj_t *ui_climate_create_temp_widget(lv_obj_t *parent,
                                        const char *label_text,
                                        bool is_hot_zone);

/**
 * @brief Créer un widget d'humidité
 * @param parent Parent
 * @return Pointeur vers le container
 */
lv_obj_t *ui_climate_create_humidity_widget(lv_obj_t *parent);

/**
 * @brief Créer un widget UV avec zone Ferguson
 * @param parent Parent
 * @param zone Zone Ferguson (1-4)
 * @return Pointeur vers le container
 */
lv_obj_t *ui_climate_create_uv_widget(lv_obj_t *parent, ferguson_zone_t zone);

/**
 * @brief Créer un widget de niveau d'eau
 * @param parent Parent
 * @param label_text "Bassin" ou "Réservoir"
 * @return Pointeur vers le container
 */
lv_obj_t *ui_climate_create_water_level_widget(lv_obj_t *parent,
                                               const char *label_text);

/**
 * @brief Créer un bouton d'équipement (toggle)
 * @param parent Parent
 * @param icon_text Icône LV_SYMBOL ou emoji
 * @param label_text Texte du bouton
 * @param is_on État initial
 * @return Pointeur vers le bouton
 */
lv_obj_t *ui_climate_create_equipment_btn(lv_obj_t *parent,
                                          const char *icon_text,
                                          const char *label_text, bool is_on);

/**
 * @brief Créer un widget de programmation horaire
 * @param parent Parent
 * @param schedule Programmation actuelle
 * @return Pointeur vers le container
 */
lv_obj_t *ui_climate_create_schedule_widget(lv_obj_t *parent,
                                            const schedule_t *schedule);

/**
 * @brief Créer une alerte visuellePe
 * @param parent Parent
 * @param alert Alerte à afficher
 * @return Pointeur vers l'item
 */
lv_obj_t *ui_climate_create_alert_item(lv_obj_t *parent, const alert_t *alert);

// ====================================================================================
// MISE À JOUR DE L'UI
// ====================================================================================

/**
 * @brief Mettre à jour le dashboard avec les données actuelles
 */
void ui_climate_update_dashboard(void);

/**
 * @brief Mettre à jour la page de détail d'un terrarium
 * @param terrarium_id ID du terrarium à afficher
 */
void ui_climate_update_terrarium_detail(uint8_t terrarium_id);

/**
 * @brief Mettre à jour les widgets de température
 * @param widget Widget créé par ui_climate_create_temp_widget
 * @param temperature Valeur en °C
 * @param target_min Consigne minimum
 * @param target_max Consigne maximum
 */
void ui_climate_update_temp_widget(lv_obj_t *widget, float temperature,
                                   float target_min, float target_max);

/**
 * @brief Mettre à jour le widget d'humidité
 * @param widget Widget créé par ui_climate_create_humidity_widget
 * @param humidity Valeur en %
 * @param target_min Consigne minimum
 * @param target_max Consigne maximum
 */
void ui_climate_update_humidity_widget(lv_obj_t *widget, float humidity,
                                       uint8_t target_min, uint8_t target_max);

/**
 * @brief Mettre à jour le widget UV
 * @param widget Widget créé par ui_climate_create_uv_widget
 * @param uv_index Valeur UV mesurée
 */
void ui_climate_update_uv_widget(lv_obj_t *widget, float uv_index);

/**
 * @brief Mettre à jour le widget de niveau d'eau
 * @param widget Widget créé par ui_climate_create_water_level_widget
 * @param level Niveau en % (0-100)
 * @param alert_threshold Seuil d'alerte
 */
void ui_climate_update_water_level_widget(lv_obj_t *widget, uint8_t level,
                                          uint8_t alert_threshold);

/**
 * @brief Mettre à jour un bouton d'équipement
 * @param btn Bouton créé par ui_climate_create_equipment_btn
 * @param is_on Nouvel état
 * @param has_error Équipement en erreur
 */
void ui_climate_update_equipment_btn(lv_obj_t *btn, bool is_on, bool has_error);

/**
 * @brief Mettre à jour la page des alertes
 */
void ui_climate_update_alerts_page(void);

// ====================================================================================
// NAVIGATION
// ====================================================================================

/**
 * @brief Afficher le dashboard
 */
void ui_climate_show_dashboard(void);

/**
 * @brief Afficher le détail d'un terrarium
 * @param terrarium_id ID du terrarium
 */
void ui_climate_show_terrarium(uint8_t terrarium_id);

/**
 * @brief Afficher la page de paramètres d'un terrarium
 * @param terrarium_id ID du terrarium
 */
void ui_climate_show_settings(uint8_t terrarium_id);

/**
 * @brief Afficher la page de programmation pour un terrarium
 * @param terrarium_id ID du terrarium
 */
void ui_climate_show_schedule(uint8_t terrarium_id);

/**
 * @brief Afficher la page Ferguson/UV
 */
void ui_climate_show_ferguson(void);

/**
 * @brief Afficher la page eau
 */
void ui_climate_show_water(void);

/**
 * @brief Afficher la page alertes
 */
void ui_climate_show_alerts(void);

/**
 * @brief Afficher la popup de sélection de type de terrarium
 */
void ui_climate_show_type_selection(void);

/**
 * @brief Obtenir le nombre d'alertes actives (pour badge)
 */
uint8_t ui_climate_get_alert_count(void);

/**
 * @brief Cacher toutes les pages climatiques (pour navigation vers autres
 * sections)
 */
void ui_climate_hide_all(void);

// ====================================================================================
// INITIALISATION
// ====================================================================================

/**
 * @brief Initialiser l'UI climatique
 * @param parent Écran parent LVGL
 */
void ui_climate_init(lv_obj_t *parent);

/**
 * @brief Tâche de mise à jour périodique de l'UI
 * @param timer Timer LVGL
 */
void ui_climate_timer_cb(lv_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif // UI_CLIMATE_H
