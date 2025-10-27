#include "climate.h"
#include "include/config.h"
#include <string.h>   // memset

// État global (module-local)
static climate_state_t g_state;

// Mutex exporté
SemaphoreHandle_t climate_measurement_mutex = NULL;

static inline float clampf(float v, float lo, float hi){
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void climate_init(void)
{
    memset(&g_state, 0, sizeof(g_state));
    // Valeurs par défaut sûres (bornes déjà définies dans config.h)
    g_state.profile.temp_c           = (CLIMATE_TEMP_MIN + CLIMATE_TEMP_MAX) * 0.5;
    g_state.profile.humidity_pct     = (CLIMATE_HUM_MIN  + CLIMATE_HUM_MAX ) * 0.5;
    g_state.profile.temp_hysteresis_c= (CLIMATE_HYST_MIN + CLIMATE_HYST_MAX) * 0.5;
    g_state.profile.uvi_max          = (CLIMATE_UVI_MIN  + CLIMATE_UVI_MAX ) * 0.5;

    // Mesure initiale neutre
    g_state.meas.t_c   = g_state.profile.temp_c;
    g_state.meas.rh_pct= g_state.profile.humidity_pct;
    g_state.meas.uvi   = 0.0f;

    if (!climate_measurement_mutex){
        climate_measurement_mutex = xSemaphoreCreateMutex();
    }
}

void climate_tick(uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    // Baseline: rien à faire (place-holder pour régulation).
    // Ici tu peux implémenter du PID/ON-OFF en fonction de g_state.profile & g_state.meas.
}

const climate_state_t* climate_get_state(void)
{
    return &g_state;
}

void climate_measurement_get(climate_meas_t* out)
{
    if (!out) return;
    // Lecture sans lock (lecture de types atomiques alignés, OK dans ce contexte simple)
    *out = g_state.meas;
}

void climate_measurement_set_locked(const climate_meas_t* m)
{
    if (!m) return;
    // Doit être appelé sous protection du mutex
    g_state.meas.t_c    = m->t_c;
    g_state.meas.rh_pct = clampf(m->rh_pct, 0.0f, 100.0f);
    g_state.meas.uvi    = clampf(m->uvi,    CLIMATE_UVI_MIN, CLIMATE_UVI_MAX);
}
