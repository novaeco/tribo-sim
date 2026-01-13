/**
 * @file reptile_engine.cpp
 * @brief Reptile Simulation Engine Implementation
 */

#include "reptile_engine.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace ReptileSim {

// ====================================================================================
// SINGLETON PATTERN
// ====================================================================================

ReptileEngine& ReptileEngine::getInstance()
{
    static ReptileEngine instance;
    return instance;
}

// ====================================================================================
// INITIALIZATION
// ====================================================================================

void ReptileEngine::init()
{
    // Initialize game state
    m_state.game_day = 1;
    m_state.game_time_hours = 12.0f; // Start at noon

    // Create initial terrarium (100x60x50 cm)
    addTerrarium(100.0f, 60.0f, 50.0f);

    // Create test reptile
    [[maybe_unused]] uint32_t rex_id = addReptile("Rex", "Pogona vitticeps");

    // Assign to first terrarium
    if (!m_state.reptiles.empty() && !m_state.terrariums.empty()) {
        m_state.reptiles[0].assigned_terrarium_id = m_state.terrariums[0].id;
    }

    // Initialize economy
    m_state.economy = {0.0f, 0.0f, 0.0f, 0.0f};

    // Initialize weather
    m_state.external_temperature = 22.0f;
    m_state.external_humidity = 50.0f;
    m_state.heatwave_active = false;
}

// ====================================================================================
// MAIN TICK (1Hz)
// ====================================================================================

void ReptileEngine::tick(float delta_time)
{
    // Update game time (1 real second = 1 game minute)
    // delta_time is in seconds, so divide by 60 to get game hours
    m_state.game_time_hours += delta_time / 60.0f;
    if (m_state.game_time_hours >= 24.0f) {
        m_state.game_time_hours -= 24.0f;
        m_state.game_day++;
    }

    // Update all 14 simulation engines
    updatePhysics(delta_time);
    updateBiology(delta_time);
    updateNutrition(delta_time);
    updateSanitary(delta_time);
    updateEconomy(delta_time);
    updateBehavior(delta_time);
    updateGenetics(delta_time);
    updateReproduction(delta_time);
    updateSocial(delta_time);
    updateSeasonal(delta_time);
    updateSecurity(delta_time);
    updateTechnical(delta_time);
    updateAdmin(delta_time);
    updateWeather(delta_time);
}

// ====================================================================================
// ENGINE UPDATES
// ====================================================================================

void ReptileEngine::updatePhysics(float dt)
{
    for (auto& terra : m_state.terrariums) {
        // Temperature simulation (simplified)
        if (terra.heater_on) {
            terra.temp_hot_zone += 0.5f * dt;
            if (terra.temp_hot_zone > 35.0f) terra.temp_hot_zone = 35.0f;
        } else {
            terra.temp_hot_zone -= 0.3f * dt;
            if (terra.temp_hot_zone < m_state.external_temperature) {
                terra.temp_hot_zone = m_state.external_temperature;
            }
        }

        terra.temp_cold_zone = terra.temp_hot_zone - 5.0f;

        // Humidity
        if (terra.mister_on) {
            terra.humidity += 1.0f * dt;
            if (terra.humidity > 80.0f) terra.humidity = 80.0f;
        } else {
            terra.humidity -= 0.5f * dt;
            if (terra.humidity < 30.0f) terra.humidity = 30.0f;
        }

        // UV (day/night cycle)
        if (m_state.game_time_hours >= 8.0f && m_state.game_time_hours <= 20.0f && terra.light_on) {
            terra.uv_index = 3.0f; // Daytime UV
        } else {
            terra.uv_index = 0.0f; // Night
        }
    }
}

void ReptileEngine::updateBiology(float dt)
{
    for (auto& reptile : m_state.reptiles) {
        // Find terrarium
        Terrarium* terra = nullptr;
        for (auto& t : m_state.terrariums) {
            if (t.id == reptile.assigned_terrarium_id) {
                terra = &t;
                break;
            }
        }

        if (!terra) {
            // No terrarium = extreme stress
            reptile.stress_level += 5.0f * dt;
            if (reptile.stress_level > 100.0f) reptile.stress_level = 100.0f;
            continue;
        }

        // Temperature stress
        if (terra->temp_hot_zone < 28.0f || terra->temp_hot_zone > 38.0f) {
            reptile.stress_level += 1.0f * dt;
        } else {
            reptile.stress_level -= 0.5f * dt;
        }

        // Clamp stress
        if (reptile.stress_level < 0.0f) reptile.stress_level = 0.0f;
        if (reptile.stress_level > 100.0f) reptile.stress_level = 100.0f;

        // Health status
        reptile.is_healthy = (reptile.stress_level < 50.0f &&
                              reptile.immune_system > 60.0f &&
                              reptile.bone_density > 60.0f);
    }
}

void ReptileEngine::updateNutrition(float dt)
{
    for (auto& reptile : m_state.reptiles) {
        // Digestion
        if (reptile.stomach_content > 0.0f) {
            reptile.stomach_content -= 0.5f * dt;
            if (reptile.stomach_content < 0.0f) reptile.stomach_content = 0.0f;
        }

        // Hunger
        reptile.is_hungry = (reptile.stomach_content < 30.0f);

        // Bone density decay without proper nutrition
        if (reptile.stomach_content < 20.0f) {
            reptile.bone_density -= 0.1f * dt;
            if (reptile.bone_density < 0.0f) reptile.bone_density = 0.0f;
        }
    }
}

void ReptileEngine::updateSanitary(float dt)
{
    for (auto& terra : m_state.terrariums) {
        // Waste accumulation
        terra.waste_level += 0.5f * dt;
        if (terra.waste_level > 100.0f) terra.waste_level = 100.0f;

        // Bacteria growth
        terra.bacteria_count += (terra.waste_level * 0.01f) * dt;
        if (terra.bacteria_count > 100.0f) terra.bacteria_count = 100.0f;
    }
}

void ReptileEngine::updateEconomy(float dt)
{
    // Electricity costs (per game hour, dt is in real seconds)
    // 1 real second = 1 game minute, so dt/60 = game hours elapsed
    float num_terrariums = static_cast<float>(m_state.terrariums.size());
    m_state.economy.electricity_cost += (num_terrariums * 0.5f) * (dt / 60.0f);

    m_state.economy.total_expenses = m_state.economy.electricity_cost +
                                     m_state.economy.food_cost +
                                     m_state.economy.veterinary_cost;
}

// ====================================================================================
// PLAYER ACTIONS
// ====================================================================================

uint32_t ReptileEngine::addReptile(const std::string& name, const std::string& species)
{
    Reptile r;
    r.id = m_next_reptile_id++;
    r.name = name;
    r.species = species;
    r.weight_grams = 350.0f;
    r.bone_density = 100.0f;
    r.hydration = 100.0f;
    r.stress_level = 0.0f;
    r.stomach_content = 50.0f;
    r.immune_system = 100.0f;
    r.is_healthy = true;
    r.is_hungry = false;
    r.is_shedding = false;
    r.assigned_terrarium_id = 0; // Not assigned

    m_state.reptiles.push_back(r);
    return r.id;
}

uint32_t ReptileEngine::addTerrarium(float width, float height, float depth)
{
    Terrarium t;
    t.id = m_next_terrarium_id++;
    t.width = width;
    t.height = height;
    t.depth = depth;
    t.temp_hot_zone = 30.0f;
    t.temp_cold_zone = 25.0f;
    t.humidity = 40.0f;
    t.uv_index = 0.0f;
    t.waste_level = 0.0f;
    t.bacteria_count = 0.0f;
    t.heater_on = true;
    t.light_on = true;
    t.mister_on = false;

    m_state.terrariums.push_back(t);
    return t.id;
}

void ReptileEngine::feedAnimal(uint32_t reptile_id)
{
    for (auto& reptile : m_state.reptiles) {
        if (reptile.id == reptile_id) {
            reptile.stomach_content += 30.0f;
            if (reptile.stomach_content > 100.0f) reptile.stomach_content = 100.0f;
            reptile.is_hungry = false;
            m_state.economy.food_cost += 2.0f; // $2 per feeding
            break;
        }
    }
}

void ReptileEngine::cleanTerrarium(uint32_t terrarium_id)
{
    for (auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            terra.waste_level = 0.0f;
            terra.bacteria_count *= 0.2f; // 80% reduction
            break;
        }
    }
}

// ====================================================================================
// SAVE/LOAD SYSTEM
// ====================================================================================

bool ReptileEngine::saveGame(const char* filepath)
{
    FILE* f = fopen(filepath, "w");
    if (!f) return false;

    // Save game state
    fprintf(f, "GAME=%lu,%.2f,%.2f,%.2f,%d\n",
            m_state.game_day,
            m_state.game_time_hours,
            m_state.external_temperature,
            m_state.external_humidity,
            m_state.heatwave_active ? 1 : 0);

    // Save economy
    fprintf(f, "ECONOMY=%.2f,%.2f,%.2f,%.2f\n",
            m_state.economy.total_expenses,
            m_state.economy.electricity_cost,
            m_state.economy.food_cost,
            m_state.economy.veterinary_cost);

    // Save reptiles
    for (const auto& r : m_state.reptiles) {
        fprintf(f, "REPTILE=%lu,%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%lu\n",
                r.id,
                r.name.c_str(),
                r.species.c_str(),
                r.weight_grams,
                r.bone_density,
                r.hydration,
                r.stress_level,
                r.stomach_content,
                r.immune_system,
                r.is_healthy ? 1 : 0,
                r.is_hungry ? 1 : 0,
                r.is_shedding ? 1 : 0,
                r.assigned_terrarium_id);
    }

    // Save terrariums
    for (const auto& t : m_state.terrariums) {
        fprintf(f, "TERRARIUM=%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d\n",
                t.id,
                t.width,
                t.height,
                t.depth,
                t.temp_hot_zone,
                t.temp_cold_zone,
                t.humidity,
                t.uv_index,
                t.waste_level,
                t.bacteria_count,
                t.heater_on ? 1 : 0,
                t.light_on ? 1 : 0,
                t.mister_on ? 1 : 0);
    }

    fclose(f);
    return true;
}

bool ReptileEngine::loadGame(const char* filepath)
{
    FILE* f = fopen(filepath, "r");
    if (!f) return false;

    char line[512];

    // Clear existing state
    m_state.reptiles.clear();
    m_state.terrariums.clear();

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "GAME=", 5) == 0) {
            int heatwave;
            sscanf(line + 5, "%lu,%f,%f,%f,%d",
                   &m_state.game_day,
                   &m_state.game_time_hours,
                   &m_state.external_temperature,
                   &m_state.external_humidity,
                   &heatwave);
            m_state.heatwave_active = (heatwave != 0);
        }
        else if (strncmp(line, "ECONOMY=", 8) == 0) {
            sscanf(line + 8, "%f,%f,%f,%f",
                   &m_state.economy.total_expenses,
                   &m_state.economy.electricity_cost,
                   &m_state.economy.food_cost,
                   &m_state.economy.veterinary_cost);
        }
        else if (strncmp(line, "REPTILE=", 8) == 0) {
            Reptile r;
            char name[64], species[64];
            int healthy, hungry, shedding;
            sscanf(line + 8, "%lu,%63[^,],%63[^,],%f,%f,%f,%f,%f,%f,%d,%d,%d,%lu",
                   &r.id,
                   name,
                   species,
                   &r.weight_grams,
                   &r.bone_density,
                   &r.hydration,
                   &r.stress_level,
                   &r.stomach_content,
                   &r.immune_system,
                   &healthy,
                   &hungry,
                   &shedding,
                   &r.assigned_terrarium_id);
            r.name = name;
            r.species = species;
            r.is_healthy = (healthy != 0);
            r.is_hungry = (hungry != 0);
            r.is_shedding = (shedding != 0);
            m_state.reptiles.push_back(r);

            // Update next ID
            if (r.id >= m_next_reptile_id) {
                m_next_reptile_id = r.id + 1;
            }
        }
        else if (strncmp(line, "TERRARIUM=", 10) == 0) {
            Terrarium t;
            int heater, light, mister;
            sscanf(line + 10, "%lu,%f,%f,%f,%f,%f,%f,%f,%f,%f,%d,%d,%d",
                   &t.id,
                   &t.width,
                   &t.height,
                   &t.depth,
                   &t.temp_hot_zone,
                   &t.temp_cold_zone,
                   &t.humidity,
                   &t.uv_index,
                   &t.waste_level,
                   &t.bacteria_count,
                   &heater,
                   &light,
                   &mister);
            t.heater_on = (heater != 0);
            t.light_on = (light != 0);
            t.mister_on = (mister != 0);
            m_state.terrariums.push_back(t);

            // Update next ID
            if (t.id >= m_next_terrarium_id) {
                m_next_terrarium_id = t.id + 1;
            }
        }
    }

    fclose(f);
    return true;
}

// ====================================================================================
// EQUIPMENT CONTROL
// ====================================================================================

void ReptileEngine::setHeater(uint32_t terrarium_id, bool on)
{
    for (auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            terra.heater_on = on;
            break;
        }
    }
}

void ReptileEngine::setLight(uint32_t terrarium_id, bool on)
{
    for (auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            terra.light_on = on;
            break;
        }
    }
}

void ReptileEngine::setMister(uint32_t terrarium_id, bool on)
{
    for (auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            terra.mister_on = on;
            break;
        }
    }
}

// ====================================================================================
// STATE GETTERS
// ====================================================================================

float ReptileEngine::getTerrariumTemp(uint32_t terrarium_id) const
{
    for (const auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            return terra.temp_hot_zone;
        }
    }
    return 0.0f;
}

float ReptileEngine::getTerrariumHumidity(uint32_t terrarium_id) const
{
    for (const auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            return terra.humidity;
        }
    }
    return 0.0f;
}

float ReptileEngine::getTerrariumWaste(uint32_t terrarium_id) const
{
    for (const auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            return terra.waste_level;
        }
    }
    return 0.0f;
}

bool ReptileEngine::getHeaterState(uint32_t terrarium_id) const
{
    for (const auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            return terra.heater_on;
        }
    }
    return false;
}

bool ReptileEngine::getLightState(uint32_t terrarium_id) const
{
    for (const auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            return terra.light_on;
        }
    }
    return false;
}

bool ReptileEngine::getMisterState(uint32_t terrarium_id) const
{
    for (const auto& terra : m_state.terrariums) {
        if (terra.id == terrarium_id) {
            return terra.mister_on;
        }
    }
    return false;
}

float ReptileEngine::getReptileStress(uint32_t reptile_id) const
{
    for (const auto& reptile : m_state.reptiles) {
        if (reptile.id == reptile_id) {
            return reptile.stress_level;
        }
    }
    return 0.0f;
}

float ReptileEngine::getReptileWeight(uint32_t reptile_id) const
{
    for (const auto& reptile : m_state.reptiles) {
        if (reptile.id == reptile_id) {
            return reptile.weight_grams;
        }
    }
    return 0.0f;
}

bool ReptileEngine::isReptileHungry(uint32_t reptile_id) const
{
    for (const auto& reptile : m_state.reptiles) {
        if (reptile.id == reptile_id) {
            return reptile.is_hungry;
        }
    }
    return false;
}

bool ReptileEngine::isReptileHealthy(uint32_t reptile_id) const
{
    for (const auto& reptile : m_state.reptiles) {
        if (reptile.id == reptile_id) {
            return reptile.is_healthy;
        }
    }
    return false;
}

// Forward declarations for external simulation engine functions
void updateBehavior(GameState& state, float dt);
void updateGenetics(GameState& state, float dt);
void updateReproduction(GameState& state, float dt);
void updateSocial(GameState& state, float dt);
void updateSeasonal(GameState& state, float dt);
void updateSecurity(GameState& state, float dt);
void updateTechnical(GameState& state, float dt);
void updateAdmin(GameState& state, float dt);
void updateWeather(GameState& state, float dt);

// Wrapper methods that call external engine functions
void ReptileEngine::updateBehavior(float dt)
{
    ReptileSim::updateBehavior(m_state, dt);
}

void ReptileEngine::updateGenetics(float dt)
{
    ReptileSim::updateGenetics(m_state, dt);
}

void ReptileEngine::updateReproduction(float dt)
{
    ReptileSim::updateReproduction(m_state, dt);
}

void ReptileEngine::updateSocial(float dt)
{
    ReptileSim::updateSocial(m_state, dt);
}

void ReptileEngine::updateSeasonal(float dt)
{
    ReptileSim::updateSeasonal(m_state, dt);
}

void ReptileEngine::updateSecurity(float dt)
{
    ReptileSim::updateSecurity(m_state, dt);
}

void ReptileEngine::updateTechnical(float dt)
{
    ReptileSim::updateTechnical(m_state, dt);
}

void ReptileEngine::updateAdmin(float dt)
{
    ReptileSim::updateAdmin(m_state, dt);
}

void ReptileEngine::updateWeather(float dt)
{
    ReptileSim::updateWeather(m_state, dt);
}

} // namespace ReptileSim

// ====================================================================================
// C INTERFACE
// ====================================================================================

extern "C" {

void reptile_engine_init(void)
{
    ReptileSim::ReptileEngine::getInstance().init();
}

void reptile_engine_tick(float delta_time)
{
    ReptileSim::ReptileEngine::getInstance().tick(delta_time);
}

uint32_t reptile_engine_get_day(void)
{
    return ReptileSim::ReptileEngine::getInstance().getState().game_day;
}

float reptile_engine_get_time_hours(void)
{
    return ReptileSim::ReptileEngine::getInstance().getState().game_time_hours;
}

int reptile_engine_get_reptile_count(void)
{
    return static_cast<int>(ReptileSim::ReptileEngine::getInstance().getState().reptiles.size());
}

int reptile_engine_get_terrarium_count(void)
{
    return static_cast<int>(ReptileSim::ReptileEngine::getInstance().getState().terrariums.size());
}

// Equipment control
void reptile_engine_set_heater(uint32_t terrarium_id, bool on)
{
    ReptileSim::ReptileEngine::getInstance().setHeater(terrarium_id, on);
}

void reptile_engine_set_light(uint32_t terrarium_id, bool on)
{
    ReptileSim::ReptileEngine::getInstance().setLight(terrarium_id, on);
}

void reptile_engine_set_mister(uint32_t terrarium_id, bool on)
{
    ReptileSim::ReptileEngine::getInstance().setMister(terrarium_id, on);
}

// Actions
void reptile_engine_feed_animal(uint32_t reptile_id)
{
    ReptileSim::ReptileEngine::getInstance().feedAnimal(reptile_id);
}

void reptile_engine_clean_terrarium(uint32_t terrarium_id)
{
    ReptileSim::ReptileEngine::getInstance().cleanTerrarium(terrarium_id);
}

// Terrarium state getters
float reptile_engine_get_terrarium_temp(uint32_t terrarium_id)
{
    return ReptileSim::ReptileEngine::getInstance().getTerrariumTemp(terrarium_id);
}

float reptile_engine_get_terrarium_humidity(uint32_t terrarium_id)
{
    return ReptileSim::ReptileEngine::getInstance().getTerrariumHumidity(terrarium_id);
}

float reptile_engine_get_terrarium_waste(uint32_t terrarium_id)
{
    return ReptileSim::ReptileEngine::getInstance().getTerrariumWaste(terrarium_id);
}

bool reptile_engine_get_heater_state(uint32_t terrarium_id)
{
    return ReptileSim::ReptileEngine::getInstance().getHeaterState(terrarium_id);
}

bool reptile_engine_get_light_state(uint32_t terrarium_id)
{
    return ReptileSim::ReptileEngine::getInstance().getLightState(terrarium_id);
}

bool reptile_engine_get_mister_state(uint32_t terrarium_id)
{
    return ReptileSim::ReptileEngine::getInstance().getMisterState(terrarium_id);
}

// Reptile state getters
float reptile_engine_get_reptile_stress(uint32_t reptile_id)
{
    return ReptileSim::ReptileEngine::getInstance().getReptileStress(reptile_id);
}

float reptile_engine_get_reptile_weight(uint32_t reptile_id)
{
    return ReptileSim::ReptileEngine::getInstance().getReptileWeight(reptile_id);
}

bool reptile_engine_is_reptile_hungry(uint32_t reptile_id)
{
    return ReptileSim::ReptileEngine::getInstance().isReptileHungry(reptile_id);
}

bool reptile_engine_is_reptile_healthy(uint32_t reptile_id)
{
    return ReptileSim::ReptileEngine::getInstance().isReptileHealthy(reptile_id);
}

// Save/Load system
bool reptile_engine_save_game(const char* filepath)
{
    return ReptileSim::ReptileEngine::getInstance().saveGame(filepath);
}

bool reptile_engine_load_game(const char* filepath)
{
    return ReptileSim::ReptileEngine::getInstance().loadGame(filepath);
}

// Add/Remove entities
uint32_t reptile_engine_add_reptile(const char* name, const char* species)
{
    return ReptileSim::ReptileEngine::getInstance().addReptile(name, species);
}

uint32_t reptile_engine_add_terrarium(float width, float height, float depth)
{
    return ReptileSim::ReptileEngine::getInstance().addTerrarium(width, height, depth);
}

} // extern "C"
