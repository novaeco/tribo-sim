#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Mesure instantanée (capteurs)
typedef struct {
    float t_c;       // Température mesurée (°C)
    float rh_pct;    // Humidité mesurée (%)
    float uvi;       // UVI mesuré
} climate_meas_t;

// Profil consignes / paramètres
typedef struct {
    double temp_c;          // Consigne T (°C)
    double humidity_pct;    // Consigne RH (%)
    double temp_hysteresis_c; // Hystérésis T (°C)
    double uvi_max;         // UVI max autorisé
} climate_profile_t;

// État agrégé du climat
typedef struct {
    climate_profile_t profile;
    climate_meas_t meas;
} climate_state_t;

// Mutex global pour la section critique de mesures
extern SemaphoreHandle_t climate_measurement_mutex;

// API
void climate_init(void);
void climate_tick(uint32_t elapsed_ms);

// Renvoie un pointeur (lecture seule) vers l'état courant
const climate_state_t* climate_get_state(void);

// Copie la mesure courante (lecture sans lock)
void climate_measurement_get(climate_meas_t* out);

// Met à jour la mesure courante (APPEL SOUS LOCK)
void climate_measurement_set_locked(const climate_meas_t* m);
