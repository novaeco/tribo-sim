/**
 * @file reptile_engine.hpp
 * @brief Reptile Simulation Engine - Singleton Pattern
 */

#ifndef REPTILE_ENGINE_HPP
#define REPTILE_ENGINE_HPP

#include "game_state.hpp"

namespace ReptileSim {

class ReptileEngine {
public:
    // Singleton access
    static ReptileEngine& getInstance();

    // Delete copy/move constructors
    ReptileEngine(const ReptileEngine&) = delete;
    ReptileEngine& operator=(const ReptileEngine&) = delete;

    // ====================================================================================
    // CORE API
    // ====================================================================================

    /**
     * @brief Initialize simulation engine
     */
    void init();

    /**
     * @brief Update simulation (call at 1Hz)
     * @param delta_time Elapsed time in seconds
     */
    void tick(float delta_time);

    /**
     * @brief Get read-only game state
     */
    const GameState& getState() const { return m_state; }

    // ====================================================================================
    // PLAYER ACTIONS
    // ====================================================================================

    /**
     * @brief Add a new reptile
     * @return Reptile ID
     */
    uint32_t addReptile(const std::string& name, const std::string& species);

    /**
     * @brief Add a new terrarium
     * @return Terrarium ID
     */
    uint32_t addTerrarium(float width, float height, float depth);

    /**
     * @brief Feed a reptile
     */
    void feedAnimal(uint32_t reptile_id);

    /**
     * @brief Clean a terrarium
     */
    void cleanTerrarium(uint32_t terrarium_id);

private:
    ReptileEngine() = default;
    ~ReptileEngine() = default;

    GameState m_state;
    uint32_t m_next_reptile_id = 1;
    uint32_t m_next_terrarium_id = 1;

    // Private engine update methods
    void updatePhysics(float dt);
    void updateBiology(float dt);
    void updateNutrition(float dt);
    void updateSanitary(float dt);
    void updateEconomy(float dt);
};

} // namespace ReptileSim

// ====================================================================================
// C INTERFACE (for integration with main.c)
// ====================================================================================

#ifdef __cplusplus
extern "C" {
#endif

void reptile_engine_init(void);
void reptile_engine_tick(float delta_time);

// Getters for C code
uint32_t reptile_engine_get_day(void);
float reptile_engine_get_time_hours(void);
int reptile_engine_get_reptile_count(void);
int reptile_engine_get_terrarium_count(void);

#ifdef __cplusplus
}
#endif

#endif // REPTILE_ENGINE_HPP
