/**
 * @file game_state.hpp
 * @brief Game State Data Structures (Pure C++)
 */

#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include <cstdint>
#include <vector>
#include <string>

namespace ReptileSim {

// ====================================================================================
// CORE DATA STRUCTURES
// ====================================================================================

struct Reptile {
    uint32_t id;
    std::string name;
    std::string species;

    // Physiology
    float weight_grams;
    float bone_density;        // 0-100%
    float hydration;           // 0-100%
    float stress_level;        // 0-100%
    float stomach_content;     // 0-100%
    float immune_system;       // 0-100%

    // Status
    bool is_healthy;
    bool is_hungry;
    bool is_shedding;

    uint32_t assigned_terrarium_id;
};

struct Terrarium {
    uint32_t id;
    float width, height, depth; // cm

    // Environment
    float temp_hot_zone;        // °C
    float temp_cold_zone;       // °C
    float humidity;             // %
    float uv_index;             // 0-10

    // Sanitary
    float waste_level;          // 0-100%
    float bacteria_count;       // 0-100%

    // Equipment
    bool heater_on;
    bool light_on;
    bool mister_on;
};

struct Economy {
    float total_expenses;
    float electricity_cost;
    float food_cost;
    float veterinary_cost;
};

// ====================================================================================
// GLOBAL GAME STATE
// ====================================================================================

struct GameState {
    // Time
    uint32_t game_day;
    float game_time_hours;      // 0-24

    // Entities
    std::vector<Reptile> reptiles;
    std::vector<Terrarium> terrariums;

    // Economy
    Economy economy;

    // Weather
    float external_temperature;
    float external_humidity;
    bool heatwave_active;
};

} // namespace ReptileSim

#endif // GAME_STATE_HPP
