/**
 * @file sim_social.cpp
 * @brief Social Engine - Hierarchy & Predation
 */

#include "../include/game_state.hpp"

namespace ReptileSim {

/**
 * @brief Update social interactions (hierarchy, predation risk)
 *
 * Simulates:
 * - Dominant/submissive resource access
 * - Intra-specific competition
 * - Inter-specific predation risk
 */
void updateSocial(GameState& state, float dt)
{
    // Check each terrarium for cohabitation
    for (auto& terra : state.terrariums) {
        // Count reptiles in this terrarium
        int reptile_count = 0;
        for (const auto& reptile : state.reptiles) {
            if (reptile.assigned_terrarium_id == terra.id) {
                reptile_count++;
            }
        }

        // Cohabitation stress (overcrowding)
        if (reptile_count > 1) {
            for (auto& reptile : state.reptiles) {
                if (reptile.assigned_terrarium_id == terra.id) {
                    // Calculate volume per animal
                    float volume = terra.width * terra.height * terra.depth;
                    float volume_per_animal = volume / reptile_count;

                    // Minimum space per animal: 200,000 cm³
                    if (volume_per_animal < 200000.0f) {
                        // Overcrowding causes social stress
                        float crowding_factor = 1.0f - (volume_per_animal / 200000.0f);
                        reptile.stress_level += crowding_factor * 1.5f * dt;

                        // Competition for food (weaker animals get less)
                        if (reptile.immune_system < 70.0f) {
                            reptile.stomach_content -= 0.3f * dt;
                            if (reptile.stomach_content < 0.0f) reptile.stomach_content = 0.0f;
                        }
                    }

                    // Hierarchy stress (submissive animals always stressed)
                    if (reptile.immune_system < 80.0f) {
                        reptile.stress_level += 0.4f * dt;
                    }
                }
            }
        }
    }

    // Clamp all stress levels
    for (auto& reptile : state.reptiles) {
        if (reptile.stress_level < 0.0f) reptile.stress_level = 0.0f;
        if (reptile.stress_level > 100.0f) reptile.stress_level = 100.0f;
    }
}

} // namespace ReptileSim
