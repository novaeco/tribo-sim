/**
 * @file sim_behavior.cpp
 * @brief Behavioral Engine - Enrichment & Stereotypic Behaviors
 */

#include "../include/game_state.hpp"

namespace ReptileSim {

/**
 * @brief Update behavioral aspects (enrichment needs, stereotypic behaviors)
 *
 * Simulates:
 * - Enclosure size adequacy
 * - Lack of enrichment leads to stress
 * - Stereotypic behaviors (pacing, rubbing) from boredom
 */
void updateBehavior(GameState& state, float dt)
{
    for (auto& reptile : state.reptiles) {
        // Find assigned terrarium
        Terrarium* terra = nullptr;
        for (auto& t : state.terrariums) {
            if (t.id == reptile.assigned_terrarium_id) {
                terra = &t;
                break;
            }
        }

        if (!terra) continue;

        // Calculate enclosure volume (cm³)
        float volume = terra->width * terra->height * terra->depth;

        // Minimum space requirement (based on animal weight)
        // Rule of thumb: 1 gram needs ~300 cm³ minimum
        float required_volume = reptile.weight_grams * 300.0f;

        // Inadequate space increases stress
        if (volume < required_volume) {
            float space_ratio = volume / required_volume;
            reptile.stress_level += (1.0f - space_ratio) * 2.0f * dt;
        } else {
            // Adequate space reduces stress (enrichment effect)
            reptile.stress_level -= 0.3f * dt;
        }

        // Clamp stress
        if (reptile.stress_level < 0.0f) reptile.stress_level = 0.0f;
        if (reptile.stress_level > 100.0f) reptile.stress_level = 100.0f;
    }
}

} // namespace ReptileSim
