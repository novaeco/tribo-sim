/**
 * @file ui_pet.h
 * @brief Interface LVGL pour le simulateur de Tribolonotus
 */

#ifndef UI_PET_H
#define UI_PET_H

#include "lvgl.h"
#include "tribolonotus_types.h"

// ====================================================================================
// INITIALISATION ET MISE À JOUR
// ====================================================================================

/**
 * @brief Initialise l'interface utilisateur
 * @param parent_screen Écran LVGL parent
 */
void ui_pet_init(lv_obj_t *parent_screen);

/**
 * @brief Mise à jour périodique de l'interface (appelé toutes les 500ms)
 */
void ui_pet_update(void);

/**
 * @brief Affiche l'écran principal (vue du lézard)
 */
void ui_pet_show_main_screen(void);

/**
 * @brief Affiche le menu de sélection des lézards
 */
void ui_pet_show_pet_list(void);

/**
 * @brief Affiche le menu des actions
 */
void ui_pet_show_actions_menu(void);

/**
 * @brief Affiche les statistiques détaillées
 */
void ui_pet_show_stats(void);

/**
 * @brief Affiche la boutique
 */
void ui_pet_show_shop(void);

/**
 * @brief Affiche le menu de création de nouveau lézard
 */
void ui_pet_show_new_pet_menu(void);

/**
 * @brief Affiche un message popup
 * @param title Titre du message
 * @param message Contenu
 */
void ui_pet_show_message(const char *title, const char *message);

#endif // UI_PET_H
