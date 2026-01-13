/**
 * @file sim_seasonal.cpp
 * @brief Seasonal Engine - Brumation & Photoperiod
 */

#include "../include/game_state.hpp"
#include <cmath>

namespace ReptileSim {

/**
 * @brief Update seasonal cycles (brumation, photoperiod)
 *
 * Simulates:
 * - Annual photoperiod variation (day length changes)
 * - Brumation requirements for temperate species
 * - Seasonal reproduction triggers
 */
void updateSeasonal(GameState& state, float dt)
{
    // Calculate day of year (1-365)
    uint32_t day_of_year = (state.game_day - 1) % 365 + 1;

    // Calculate photoperiod (hours of daylight)
    // Peak at day 172 (summer solstice), minimum at day 355 (winter solstice)
    const float PI = 3.14159265f;
    float photoperiod_hours = 12.0f + 2.5f * std::sin(2.0f * PI * (day_of_year - 80) / 365.0f);

    // Brumation season (days 300-365 and 1-60 = winter)
    bool brumation_season = (day_of_year >= 300 || day_of_year <= 60);

    for (auto& reptile : state.reptiles) {
        // Find terrarium
        Terrarium* terra = nullptr;
        for (auto& t : state.terrariums) {
            if (t.id == reptile.assigned_terrarium_id) {
                terra = &t;
                break;
            }
        }

        if (!terra) continue;

        // If brumation season and temperature is kept high, increase stress
        if (brumation_season && terra->temp_hot_zone > 25.0f) {
            reptile.stress_level += 0.5f * dt;
        }

        // Photoperiod mismatch (lights on during "night" hours)
        float natural_night_start = 12.0f + photoperiod_hours / 2.0f;
        bool should_be_dark = (state.game_time_hours < (24.0f - natural_night_start) ||
                               state.game_time_hours > natural_night_start);

        if (terra->light_on && should_be_dark) {
            reptile.stress_level += 0.2f * dt;
        }

        // Clamp stress
        if (reptile.stress_level < 0.0f) reptile.stress_level = 0.0f;
        if (reptile.stress_level > 100.0f) reptile.stress_level = 100.0f;
    }
}

} // namespace ReptileSim
