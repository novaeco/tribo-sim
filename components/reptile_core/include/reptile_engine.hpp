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

    /**
     * @brief Save complete game state to SPIFFS
     * @return true if successful
     */
    bool saveGame(const char* filepath);

    /**
     * @brief Load complete game state from SPIFFS
     * @return true if successful
     */
    bool loadGame(const char* filepath);

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

    // ====================================================================================
    // EQUIPMENT CONTROL
    // ====================================================================================

    /**
     * @brief Set heater state for a terrarium
     */
    void setHeater(uint32_t terrarium_id, bool on);

    /**
     * @brief Set light state for a terrarium
     */
    void setLight(uint32_t terrarium_id, bool on);

    /**
     * @brief Set mister state for a terrarium
     */
    void setMister(uint32_t terrarium_id, bool on);

    // ====================================================================================
    // STATE GETTERS (for UI)
    // ====================================================================================

    /**
     * @brief Get terrarium temperature (hot zone)
     */
    float getTerrariumTemp(uint32_t terrarium_id) const;

    /**
     * @brief Get terrarium humidity
     */
    float getTerrariumHumidity(uint32_t terrarium_id) const;

    /**
     * @brief Get terrarium waste level
     */
    float getTerrariumWaste(uint32_t terrarium_id) const;

    /**
     * @brief Get heater state
     */
    bool getHeaterState(uint32_t terrarium_id) const;

    /**
     * @brief Get light state
     */
    bool getLightState(uint32_t terrarium_id) const;

    /**
     * @brief Get mister state
     */
    bool getMisterState(uint32_t terrarium_id) const;

    /**
     * @brief Get reptile stress level
     */
    float getReptileStress(uint32_t reptile_id) const;

    /**
     * @brief Get reptile weight
     */
    float getReptileWeight(uint32_t reptile_id) const;

    /**
     * @brief Check if reptile is hungry
     */
    bool isReptileHungry(uint32_t reptile_id) const;

    /**
     * @brief Check if reptile is healthy
     */
    bool isReptileHealthy(uint32_t reptile_id) const;

private:
    ReptileEngine() = default;
    ~ReptileEngine() = default;

    GameState m_state;
    uint32_t m_next_reptile_id = 1;
    uint32_t m_next_terrarium_id = 1;

    // Private engine update methods (14 simulation engines)
    void updatePhysics(float dt);
    void updateBiology(float dt);
    void updateNutrition(float dt);
    void updateSanitary(float dt);
    void updateEconomy(float dt);
    void updateBehavior(float dt);
    void updateGenetics(float dt);
    void updateReproduction(float dt);
    void updateSocial(float dt);
    void updateSeasonal(float dt);
    void updateSecurity(float dt);
    void updateTechnical(float dt);
    void updateAdmin(float dt);
    void updateWeather(float dt);
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

// Equipment control
void reptile_engine_set_heater(uint32_t terrarium_id, bool on);
void reptile_engine_set_light(uint32_t terrarium_id, bool on);
void reptile_engine_set_mister(uint32_t terrarium_id, bool on);

// Actions
void reptile_engine_feed_animal(uint32_t reptile_id);
void reptile_engine_clean_terrarium(uint32_t terrarium_id);

// Terrarium state getters
float reptile_engine_get_terrarium_temp(uint32_t terrarium_id);
float reptile_engine_get_terrarium_humidity(uint32_t terrarium_id);
float reptile_engine_get_terrarium_waste(uint32_t terrarium_id);
bool reptile_engine_get_heater_state(uint32_t terrarium_id);
bool reptile_engine_get_light_state(uint32_t terrarium_id);
bool reptile_engine_get_mister_state(uint32_t terrarium_id);

// Reptile state getters
float reptile_engine_get_reptile_stress(uint32_t reptile_id);
float reptile_engine_get_reptile_weight(uint32_t reptile_id);
bool reptile_engine_is_reptile_hungry(uint32_t reptile_id);
bool reptile_engine_is_reptile_healthy(uint32_t reptile_id);

// Save/Load system
bool reptile_engine_save_game(const char* filepath);
bool reptile_engine_load_game(const char* filepath);

// Add/Remove entities
uint32_t reptile_engine_add_reptile(const char* name, const char* species);
uint32_t reptile_engine_add_terrarium(float width, float height, float depth);

#ifdef __cplusplus
}
#endif

#endif // REPTILE_ENGINE_HPP
