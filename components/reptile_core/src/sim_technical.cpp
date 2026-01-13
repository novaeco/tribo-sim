/**
 * @file sim_technical.cpp
 * @brief Technical Engine - Equipment Failures & Power Outages
 */

#include "../include/game_state.hpp"
#include <cstdlib>

namespace ReptileSim {

// Simple pseudo-random for equipment failure simulation
static uint32_t simple_rand_state = 12345;

static float simple_random()
{
    simple_rand_state = simple_rand_state * 1103515245 + 12345;
    return (float)(simple_rand_state % 10000) / 10000.0f;
}

/**
 * @brief Update technical aspects (equipment MTBF, failures)
 *
 * Simulates:
 * - Mean Time Between Failures (MTBF) for equipment
 * - Power outages (crisis events)
 * - Equipment degradation over time
 */
void updateTechnical(GameState& state, float dt)
{
    for (auto& terra : state.terrariums) {
        // Equipment failure probability (very low, but increases over time)
        // Assume MTBF = 8760 hours (1 year) for heaters
        // Failure rate per second = 1 / (MTBF * 3600)
        float heater_failure_rate = 1.0f / (8760.0f * 3600.0f);

        // Random heater failure
        if (terra.heater_on && simple_random() < (heater_failure_rate * dt)) {
            terra.heater_on = false;
            // In a real system, this would trigger an alert
        }

        // Light failure (slightly more frequent, MTBF = 5000 hours)
        float light_failure_rate = 1.0f / (5000.0f * 3600.0f);
        if (terra.light_on && simple_random() < (light_failure_rate * dt)) {
            terra.light_on = false;
        }

        // Mister failure (MTBF = 3000 hours)
        float mister_failure_rate = 1.0f / (3000.0f * 3600.0f);
        if (terra.mister_on && simple_random() < (mister_failure_rate * dt)) {
            terra.mister_on = false;
        }

        // Power outage simulation (very rare: 0.01% chance per game day)
        if (simple_random() < (0.0001f * dt / 86400.0f)) {
            // All equipment fails simultaneously
            terra.heater_on = false;
            terra.light_on = false;
            terra.mister_on = false;
        }
    }

    // Increase electricity cost slightly for aging equipment
    state.economy.electricity_cost += 0.001f * dt;
}

} // namespace ReptileSim
