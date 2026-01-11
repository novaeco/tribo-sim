/**
 * @file climate_manager.c
 * @brief Climate Control Manager - Implementation
 * @version 1.0
 * @date 2026-01-06
 *
 * Moteur de simulation temps réel pour la gestion climatique.
 */

#include "climate_manager.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "CLIMATE_MGR";

// ====================================================================================
// DONNÉES GLOBALES
// ====================================================================================

static terrarium_config_t terrariums[MAX_TERRARIUMS];
static uint8_t terrarium_count = 0;

static alert_t alerts[MAX_ALERTS];
static uint8_t alert_count = 0;
static uint8_t next_alert_id = 1;

static history_point_t history[MAX_TERRARIUMS][MAX_HISTORY_POINTS];
static uint16_t history_index[MAX_TERRARIUMS] = {0};
static uint16_t history_count[MAX_TERRARIUMS] = {0};

static TaskHandle_t simulation_task_handle = NULL;
static SemaphoreHandle_t climate_mutex = NULL;
static bool simulation_running = false;
static float time_multiplier = 1.0f;
static comm_mode_t current_comm_mode = COMM_MODE_NONE;

// ====================================================================================
// FONCTIONS UTILITAIRES INTERNES
// ====================================================================================

/**
 * @brief Générer un nombre aléatoire flottant dans une plage
 */
static float random_float(float min, float max) {
  uint32_t rand_val = esp_random();
  float normalized = (float)rand_val / (float)UINT32_MAX;
  return min + normalized * (max - min);
}

/**
 * @brief Vérifier si l'heure actuelle est dans une plage horaire
 */
static bool is_time_in_schedule(const schedule_t *schedule) {
  if (!schedule->enabled)
    return false;

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  int current_day = (timeinfo.tm_wday + 6) % 7; // Convertir dim=0 en lun=0
  if (!schedule->days[current_day])
    return false;

  int current_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int on_minutes = schedule->on_hour * 60 + schedule->on_minute;
  int off_minutes = schedule->off_hour * 60 + schedule->off_minute;

  if (on_minutes <= off_minutes) {
    return current_minutes >= on_minutes && current_minutes < off_minutes;
  } else {
    // Passage minuit
    return current_minutes >= on_minutes || current_minutes < off_minutes;
  }
}

/**
 * @brief Vérifier si c'est le jour (entre lever et coucher du soleil simulé)
 */
static bool is_daytime(const terrarium_config_t *t) {
  return is_time_in_schedule(&t->light_schedule);
}

/**
 * @brief Créer une alerte
 */
static void create_alert(uint8_t terrarium_id, alert_type_t type,
                         alert_priority_t priority, const char *message) {
  if (alert_count >= MAX_ALERTS) {
    // Supprimer la plus ancienne alerte acquittée
    for (int i = 0; i < alert_count; i++) {
      if (alerts[i].acknowledged) {
        memmove(&alerts[i], &alerts[i + 1],
                (alert_count - i - 1) * sizeof(alert_t));
        alert_count--;
        break;
      }
    }
    if (alert_count >= MAX_ALERTS) {
      ESP_LOGW(TAG, "Alert buffer full, cannot create new alert");
      return;
    }
  }

  // Vérifier si une alerte similaire existe déjà
  for (int i = 0; i < alert_count; i++) {
    if (alerts[i].terrarium_id == terrarium_id && alerts[i].type == type &&
        alerts[i].active) {
      return; // Alerte déjà existante
    }
  }

  alert_t *a = &alerts[alert_count];
  a->id = next_alert_id++;
  a->terrarium_id = terrarium_id;
  a->type = type;
  a->priority = priority;
  time(&a->timestamp);
  strncpy(a->message, message, sizeof(a->message) - 1);
  a->message[sizeof(a->message) - 1] = '\0';
  a->acknowledged = false;
  a->active = true;

  alert_count++;
  ESP_LOGW(TAG, "Alert created: T%d - %s", terrarium_id, message);
}

/**
 * @brief Désactiver une alerte par type
 */
static void deactivate_alert(uint8_t terrarium_id, alert_type_t type) {
  for (int i = 0; i < alert_count; i++) {
    if (alerts[i].terrarium_id == terrarium_id && alerts[i].type == type &&
        alerts[i].active) {
      alerts[i].active = false;
      ESP_LOGI(TAG, "Alert deactivated: T%d type %d", terrarium_id, type);
    }
  }
}

// ====================================================================================
// SIMULATION DES CAPTEURS
// ====================================================================================

/**
 * @brief Simuler la température
 */
static void simulate_temperature(terrarium_config_t *t) {
  sensor_data_t *s = &t->sensors;
  equipment_state_data_t *e = &t->equipment;

  bool daytime = is_daytime(t);

  // Températures cibles selon jour/nuit
  float target_hot, target_cold;
  if (daytime) {
    target_hot = (t->temp_day_hot_min + t->temp_day_hot_max) / 2.0f;
    target_cold = (t->temp_day_cold_min + t->temp_day_cold_max) / 2.0f;
  } else {
    target_hot = (t->temp_night_min + t->temp_night_max) / 2.0f;
    target_cold = t->temp_night_min;
  }

  // Influence du chauffage
  float heating_effect = 0.0f;
  if (e->heating_on && !e->heating_error) {
    heating_effect = 0.3f * (e->heating_power / 100.0f) * time_multiplier;
  }

  // Refroidissement naturel
  float cooling_rate = 0.05f * time_multiplier;

  // Zone chaude
  if (s->temp_hot_zone < target_hot) {
    s->temp_hot_zone += heating_effect;
  } else {
    s->temp_hot_zone -= cooling_rate;
  }

  // Zone froide (moins affectée par le chauffage)
  if (s->temp_cold_zone < target_cold) {
    s->temp_cold_zone += heating_effect * 0.3f;
  } else {
    s->temp_cold_zone -= cooling_rate * 1.5f;
  }

  // Ajouter du bruit
  s->temp_hot_zone += random_float(-0.2f, 0.2f);
  s->temp_cold_zone += random_float(-0.15f, 0.15f);

  // Limites physiques
  s->temp_hot_zone =
      fmaxf(TEMP_MIN_VALID, fminf(TEMP_MAX_VALID, s->temp_hot_zone));
  s->temp_cold_zone =
      fmaxf(TEMP_MIN_VALID, fminf(TEMP_MAX_VALID, s->temp_cold_zone));

  // Vérification alertes température
  float hot_target = daytime ? t->temp_day_hot_max : t->temp_night_max;
  float cold_target = daytime ? t->temp_day_cold_min : t->temp_night_min;

  if (s->temp_hot_zone > hot_target + t->temp_alert_threshold) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Temp. zone chaude élevée: %.1f°C",
             s->temp_hot_zone);
    create_alert(t->id, ALERT_TEMP_HIGH, ALERT_PRIORITY_WARNING, msg);
  } else {
    deactivate_alert(t->id, ALERT_TEMP_HIGH);
  }

  if (s->temp_cold_zone < cold_target - t->temp_alert_threshold) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Temp. zone froide basse: %.1f°C",
             s->temp_cold_zone);
    create_alert(t->id, ALERT_TEMP_LOW, ALERT_PRIORITY_WARNING, msg);
  } else {
    deactivate_alert(t->id, ALERT_TEMP_LOW);
  }
}

/**
 * @brief Simuler l'humidité
 */
static void simulate_humidity(terrarium_config_t *t) {
  sensor_data_t *s = &t->sensors;
  equipment_state_data_t *e = &t->equipment;

  float target_humidity = (t->humidity_min + t->humidity_max) / 2.0f;

  // Effet de la brumisation
  if (e->misting_on && !e->misting_error) {
    s->humidity += 5.0f * time_multiplier;
  }

  // Évaporation naturelle (plus rapide si chaud)
  float evap_rate = 0.3f * (s->temp_hot_zone / 30.0f) * time_multiplier;
  s->humidity -= evap_rate;

  // Tendance vers l'équilibre
  if (s->humidity < target_humidity) {
    s->humidity += 0.1f * time_multiplier;
  }

  // Bruit
  s->humidity += random_float(-1.0f, 1.0f);

  // Limites
  s->humidity = fmaxf(0.0f, fminf(100.0f, s->humidity));

  // Alertes humidité
  if (s->humidity > t->humidity_max + 10) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Humidité élevée: %.0f%%", s->humidity);
    create_alert(t->id, ALERT_HUMIDITY_HIGH, ALERT_PRIORITY_INFO, msg);
  } else {
    deactivate_alert(t->id, ALERT_HUMIDITY_HIGH);
  }

  if (s->humidity < t->humidity_min - 10) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Humidité basse: %.0f%%", s->humidity);
    create_alert(t->id, ALERT_HUMIDITY_LOW, ALERT_PRIORITY_INFO, msg);
  } else {
    deactivate_alert(t->id, ALERT_HUMIDITY_LOW);
  }
}

/**
 * @brief Simuler les niveaux d'eau
 */
static void simulate_water_levels(terrarium_config_t *t) {
  sensor_data_t *s = &t->sensors;
  equipment_state_data_t *e = &t->equipment;

  // Évaporation bassin
  if (s->water_basin_level > 0) {
    float evap = 0.02f * time_multiplier;
    s->water_basin_level = (uint8_t)fmaxf(0, s->water_basin_level - evap);
  }

  // Consommation réservoir (brumisation)
  if (e->misting_on && s->water_reservoir_level > 0) {
    s->water_reservoir_level = (uint8_t)fmaxf(0, s->water_reservoir_level - 2);
  }

  // Alertes niveau d'eau
  if (s->water_basin_level < t->water_basin_alert) {
    create_alert(t->id, ALERT_WATER_BASIN_LOW, ALERT_PRIORITY_WARNING,
                 "Niveau bassin bas - Remplir");
  } else {
    deactivate_alert(t->id, ALERT_WATER_BASIN_LOW);
  }

  if (s->water_reservoir_level < t->water_reservoir_alert) {
    create_alert(t->id, ALERT_WATER_RESERVOIR_LOW, ALERT_PRIORITY_WARNING,
                 "Réservoir brumisation bas");
  } else {
    deactivate_alert(t->id, ALERT_WATER_RESERVOIR_LOW);
  }
}

/**
 * @brief Simuler l'index UV
 */
static void simulate_uv_index(terrarium_config_t *t) {
  sensor_data_t *s = &t->sensors;
  equipment_state_data_t *e = &t->equipment;

  const ferguson_zone_info_t *zone = climate_get_ferguson_info(t->uv_zone);
  float target_uv = (zone->uvi_min + zone->uvi_max) / 2.0f;

  if (e->uv_lamp_on && !e->uv_lamp_error) {
    // UV proportionnel à l'intensité
    s->uv_index = target_uv * (e->uv_intensity / 100.0f);
  } else {
    // Décroissance
    s->uv_index *= 0.9f;
  }

  // Bruit
  s->uv_index += random_float(-0.1f, 0.1f);
  s->uv_index = fmaxf(0.0f, fminf(UV_INDEX_MAX, s->uv_index));
}

/**
 * @brief Mettre à jour les équipements selon les programmations
 */
static void update_equipment_schedules(terrarium_config_t *t) {
  equipment_state_data_t *e = &t->equipment;

  // Éclairage jour
  e->day_light_on = is_time_in_schedule(&t->light_schedule);

  // Lampe UV
  bool uv_scheduled = is_time_in_schedule(&t->uv_schedule);
  if (uv_scheduled && !e->uv_lamp_error) {
    e->uv_lamp_on = true;
    e->uv_intensity = 100;
  } else if (!uv_scheduled) {
    e->uv_lamp_on = false;
    e->uv_intensity = 0;
  }

  // Chauffage (thermostat automatique)
  if (is_time_in_schedule(&t->heating_schedule)) {
    bool daytime = is_daytime(t);
    float target = daytime ? (t->temp_day_hot_min + t->temp_day_hot_max) / 2.0f
                           : (t->temp_night_min + t->temp_night_max) / 2.0f;

    if (t->sensors.temp_hot_zone < target - 1.0f) {
      e->heating_on = true;
      e->heating_power = 100;
    } else if (t->sensors.temp_hot_zone > target + 1.0f) {
      e->heating_on = false;
      e->heating_power = 0;
    }
  } else {
    e->heating_on = false;
    e->heating_power = 0;
  }

  // Pompe
  e->pump_on = is_time_in_schedule(&t->pump_schedule);

  // Brumisation
  if (t->misting.enabled) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_hour >= t->misting.start_hour &&
        timeinfo.tm_hour < t->misting.end_hour) {

      uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
      uint32_t interval_ms = t->misting.interval_minutes * 60 * 1000;

      if (t->misting.interval_minutes > 0 && interval_ms > 0) {
        if (now_ms - e->misting_last_cycle >= interval_ms) {
          e->misting_on = true;
          e->misting_last_cycle = now_ms;
          ESP_LOGI(TAG, "T%d: Misting cycle started", t->id);

          // Timer pour arrêter (simplifié - sera géré au prochain cycle)
        }
      }
    }

    // Arrêter après durée
    if (e->misting_on) {
      uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (now_ms - e->misting_last_cycle >=
          (uint32_t)(t->misting.duration_seconds * 1000)) {
        e->misting_on = false;
      }
    }
  }
}

/**
 * @brief Sauvegarder un point d'historique
 */
static void save_history_point(terrarium_config_t *t) {
  uint8_t idx = t->id;
  if (idx >= MAX_TERRARIUMS)
    return;

  history_point_t *h = &history[idx][history_index[idx]];
  time(&h->timestamp);
  h->temp_hot = t->sensors.temp_hot_zone;
  h->temp_cold = t->sensors.temp_cold_zone;
  h->humidity = t->sensors.humidity;
  h->uv_index = t->sensors.uv_index;

  history_index[idx] = (history_index[idx] + 1) % MAX_HISTORY_POINTS;
  if (history_count[idx] < MAX_HISTORY_POINTS) {
    history_count[idx]++;
  }
}

// ====================================================================================
// TÂCHE DE SIMULATION
// ====================================================================================

static void simulation_task(void *pvParameters) {
  ESP_LOGI(TAG, "Climate simulation task started");

  TickType_t last_sensor_update = 0;
  TickType_t last_history_save = 0;

  while (simulation_running) {
    TickType_t now = xTaskGetTickCount();

    // Mise à jour capteurs (1 seconde)
    if ((now - last_sensor_update) * portTICK_PERIOD_MS >=
        SENSOR_UPDATE_INTERVAL_MS) {
      last_sensor_update = now;

      if (xSemaphoreTake(climate_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < terrarium_count; i++) {
          if (terrariums[i].active) {
            update_equipment_schedules(&terrariums[i]);
            simulate_temperature(&terrariums[i]);
            simulate_humidity(&terrariums[i]);
            simulate_water_levels(&terrariums[i]);
            simulate_uv_index(&terrariums[i]);
            time(&terrariums[i].sensors.last_update);
          }
        }
        xSemaphoreGive(climate_mutex);
      }
    }

    // Sauvegarde historique (5 minutes)
    if ((now - last_history_save) * portTICK_PERIOD_MS >=
        HISTORY_SAVE_INTERVAL_MS) {
      last_history_save = now;

      if (xSemaphoreTake(climate_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < terrarium_count; i++) {
          if (terrariums[i].active) {
            save_history_point(&terrariums[i]);
          }
        }
        xSemaphoreGive(climate_mutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms loop
  }

  ESP_LOGI(TAG, "Climate simulation task stopped");
  vTaskDelete(NULL);
}

// ====================================================================================
// API PUBLIQUE
// ====================================================================================

esp_err_t climate_manager_init(void) {
  ESP_LOGI(TAG, "Initializing Climate Manager...");

  // Créer mutex
  climate_mutex = xSemaphoreCreateMutex();
  if (!climate_mutex) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }

  // Initialiser les tableaux
  memset(terrariums, 0, sizeof(terrariums));
  memset(alerts, 0, sizeof(alerts));
  memset(history, 0, sizeof(history));

  terrarium_count = 0;
  alert_count = 0;

  ESP_LOGI(TAG, "Climate Manager initialized");
  return ESP_OK;
}

esp_err_t climate_manager_start(void) {
  if (simulation_running) {
    return ESP_OK;
  }

  simulation_running = true;

  BaseType_t ret = xTaskCreatePinnedToCore(simulation_task, "climate_sim", 4096,
                                           NULL, 5, &simulation_task_handle,
                                           1 // Core 1
  );

  if (ret != pdPASS) {
    simulation_running = false;
    ESP_LOGE(TAG, "Failed to create simulation task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Climate simulation started");
  return ESP_OK;
}

void climate_manager_stop(void) {
  simulation_running = false;
  if (simulation_task_handle) {
    vTaskDelay(pdMS_TO_TICKS(200)); // Laisser la tâche s'arrêter
    simulation_task_handle = NULL;
  }
}

int climate_add_terrarium(terrarium_type_t type, const char *name) {
  if (terrarium_count >= MAX_TERRARIUMS) {
    ESP_LOGE(TAG, "Max terrariums reached");
    return -1;
  }

  if (xSemaphoreTake(climate_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return -1;
  }

  // Copier le preset
  const terrarium_config_t *preset = climate_get_preset(type);
  terrarium_config_t *t = &terrariums[terrarium_count];
  memcpy(t, preset, sizeof(terrarium_config_t));

  // Assigner ID
  t->id = terrarium_count;

  // Nom personnalisé
  if (name && name[0]) {
    strncpy(t->name, name, sizeof(t->name) - 1);
  } else {
    snprintf(t->name, sizeof(t->name), "%s #%d", climate_get_type_name(type),
             terrarium_count + 1);
  }

  // Initialiser les capteurs avec des valeurs réalistes
  t->sensors.temp_hot_zone = (t->temp_day_hot_min + t->temp_day_hot_max) / 2.0f;
  t->sensors.temp_cold_zone =
      (t->temp_day_cold_min + t->temp_day_cold_max) / 2.0f;
  t->sensors.humidity = (t->humidity_min + t->humidity_max) / 2.0f;
  t->sensors.uv_index = 0.0f;
  t->sensors.water_basin_level = 80;
  t->sensors.water_reservoir_level = 90;
  time(&t->sensors.last_update);

  // Équipements à OFF
  memset(&t->equipment, 0, sizeof(t->equipment));

  t->active = true;

  int id = terrarium_count;
  terrarium_count++;

  xSemaphoreGive(climate_mutex);

  ESP_LOGI(TAG, "Terrarium added: ID=%d, Name=%s, Type=%s", id, t->name,
           climate_get_type_name(type));

  return id;
}

esp_err_t climate_remove_terrarium(uint8_t id) {
  if (id >= terrarium_count) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(climate_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  terrariums[id].active = false;

  xSemaphoreGive(climate_mutex);

  ESP_LOGI(TAG, "Terrarium %d removed", id);
  return ESP_OK;
}

terrarium_config_t *climate_get_terrarium(uint8_t id) {
  if (id >= terrarium_count) {
    return NULL;
  }
  return &terrariums[id];
}

uint8_t climate_get_terrarium_count(void) {
  uint8_t count = 0;
  for (int i = 0; i < terrarium_count; i++) {
    if (terrariums[i].active)
      count++;
  }
  return count;
}

uint8_t climate_get_all_terrariums(terrarium_config_t **out_terrariums) {
  uint8_t count = 0;
  for (int i = 0; i < terrarium_count && count < MAX_TERRARIUMS; i++) {
    if (terrariums[i].active) {
      out_terrariums[count++] = &terrariums[i];
    }
  }
  return count;
}

const sensor_data_t *climate_get_sensors(uint8_t id) {
  if (id >= terrarium_count) {
    return NULL;
  }
  return &terrariums[id].sensors;
}

// Helper functions for UI
const char *climate_get_terrarium_name(uint8_t id) {
  if (id >= terrarium_count)
    return "N/A";
  return terrariums[id].name;
}

float climate_get_temperature(uint8_t id) {
  if (id >= terrarium_count)
    return 0.0f;
  return terrariums[id].sensors.temp_hot_zone;
}

float climate_get_humidity(uint8_t id) {
  if (id >= terrarium_count)
    return 0.0f;
  return terrariums[id].sensors.humidity;
}

bool climate_is_heating_on(uint8_t id) {
  if (id >= terrarium_count)
    return false;
  return terrariums[id].equipment.heating_on;
}

bool climate_is_uv_on(uint8_t id) {
  if (id >= terrarium_count)
    return false;
  return terrariums[id].equipment.uv_lamp_on;
}

void climate_update_sensors(uint8_t id) {
  if (id >= terrarium_count)
    return;

  if (xSemaphoreTake(climate_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    simulate_temperature(&terrariums[id]);
    simulate_humidity(&terrariums[id]);
    simulate_water_levels(&terrariums[id]);
    simulate_uv_index(&terrariums[id]);
    xSemaphoreGive(climate_mutex);
  }
}

esp_err_t climate_set_heating(uint8_t id, bool on, uint8_t power) {
  if (id >= terrarium_count)
    return ESP_ERR_INVALID_ARG;

  terrariums[id].equipment.heating_on = on;
  terrariums[id].equipment.heating_power = power;

  ESP_LOGI(TAG, "T%d: Heating %s (power=%d%%)", id, on ? "ON" : "OFF", power);
  return ESP_OK;
}

esp_err_t climate_set_uv_lamp(uint8_t id, bool on, uint8_t intensity) {
  if (id >= terrarium_count)
    return ESP_ERR_INVALID_ARG;

  terrariums[id].equipment.uv_lamp_on = on;
  terrariums[id].equipment.uv_intensity = intensity;

  ESP_LOGI(TAG, "T%d: UV lamp %s (intensity=%d%%)", id, on ? "ON" : "OFF",
           intensity);
  return ESP_OK;
}

esp_err_t climate_set_day_light(uint8_t id, bool on) {
  if (id >= terrarium_count)
    return ESP_ERR_INVALID_ARG;

  terrariums[id].equipment.day_light_on = on;

  ESP_LOGI(TAG, "T%d: Day light %s", id, on ? "ON" : "OFF");
  return ESP_OK;
}

esp_err_t climate_trigger_misting(uint8_t id) {
  if (id >= terrarium_count)
    return ESP_ERR_INVALID_ARG;

  terrariums[id].equipment.misting_on = true;
  terrariums[id].equipment.misting_last_cycle =
      xTaskGetTickCount() * portTICK_PERIOD_MS;

  ESP_LOGI(TAG, "T%d: Misting triggered", id);
  return ESP_OK;
}

esp_err_t climate_set_pump(uint8_t id, bool on) {
  if (id >= terrarium_count)
    return ESP_ERR_INVALID_ARG;

  terrariums[id].equipment.pump_on = on;

  ESP_LOGI(TAG, "T%d: Pump %s", id, on ? "ON" : "OFF");
  return ESP_OK;
}

const equipment_state_data_t *climate_get_equipment_state(uint8_t id) {
  if (id >= terrarium_count)
    return NULL;
  return &terrariums[id].equipment;
}

esp_err_t climate_set_light_schedule(uint8_t id, const schedule_t *schedule) {
  if (id >= terrarium_count || !schedule)
    return ESP_ERR_INVALID_ARG;
  memcpy(&terrariums[id].light_schedule, schedule, sizeof(schedule_t));
  return ESP_OK;
}

esp_err_t climate_set_uv_schedule(uint8_t id, const schedule_t *schedule) {
  if (id >= terrarium_count || !schedule)
    return ESP_ERR_INVALID_ARG;
  memcpy(&terrariums[id].uv_schedule, schedule, sizeof(schedule_t));
  return ESP_OK;
}

esp_err_t climate_set_misting_schedule(uint8_t id,
                                       const misting_schedule_t *schedule) {
  if (id >= terrarium_count || !schedule)
    return ESP_ERR_INVALID_ARG;
  memcpy(&terrariums[id].misting, schedule, sizeof(misting_schedule_t));
  return ESP_OK;
}

esp_err_t climate_set_pump_schedule(uint8_t id, const schedule_t *schedule) {
  if (id >= terrarium_count || !schedule)
    return ESP_ERR_INVALID_ARG;
  memcpy(&terrariums[id].pump_schedule, schedule, sizeof(schedule_t));
  return ESP_OK;
}

uint8_t climate_get_active_alert_count(void) {
  uint8_t count = 0;
  for (int i = 0; i < alert_count; i++) {
    if (alerts[i].active && !alerts[i].acknowledged) {
      count++;
    }
  }
  return count;
}

uint8_t climate_get_active_alerts(alert_t *out_alerts) {
  uint8_t count = 0;
  for (int i = 0; i < alert_count && count < MAX_ALERTS; i++) {
    if (alerts[i].active) {
      memcpy(&out_alerts[count++], &alerts[i], sizeof(alert_t));
    }
  }
  return count;
}

esp_err_t climate_acknowledge_alert(uint8_t alert_id) {
  for (int i = 0; i < alert_count; i++) {
    if (alerts[i].id == alert_id) {
      alerts[i].acknowledged = true;
      return ESP_OK;
    }
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t climate_acknowledge_all_alerts(uint8_t terrarium_id) {
  for (int i = 0; i < alert_count; i++) {
    if (alerts[i].terrarium_id == terrarium_id) {
      alerts[i].acknowledged = true;
    }
  }
  return ESP_OK;
}

void climate_clear_acknowledged_alerts(void) {
  int write_idx = 0;
  for (int i = 0; i < alert_count; i++) {
    if (!alerts[i].acknowledged || alerts[i].active) {
      if (write_idx != i) {
        memcpy(&alerts[write_idx], &alerts[i], sizeof(alert_t));
      }
      write_idx++;
    }
  }
  alert_count = write_idx;
}

uint16_t climate_get_history(uint8_t id, history_point_t *points,
                             uint16_t max_points) {
  if (id >= MAX_TERRARIUMS || !points)
    return 0;

  uint16_t count = 0;
  uint16_t total = history_count[id];
  uint16_t start_idx =
      (history_index[id] - total + MAX_HISTORY_POINTS) % MAX_HISTORY_POINTS;

  for (uint16_t i = 0; i < total && count < max_points; i++) {
    uint16_t idx = (start_idx + i) % MAX_HISTORY_POINTS;
    memcpy(&points[count++], &history[id][idx], sizeof(history_point_t));
  }

  return count;
}

void climate_set_time_multiplier(float multiplier) {
  time_multiplier = fmaxf(0.1f, fminf(100.0f, multiplier));
  ESP_LOGI(TAG, "Time multiplier set to %.1fx", time_multiplier);
}

esp_err_t climate_refill_water(uint8_t id, uint8_t basin_level,
                               uint8_t reservoir_level) {
  if (id >= terrarium_count)
    return ESP_ERR_INVALID_ARG;

  terrariums[id].sensors.water_basin_level =
      basin_level > 100 ? 100 : basin_level;
  terrariums[id].sensors.water_reservoir_level =
      reservoir_level > 100 ? 100 : reservoir_level;

  ESP_LOGI(TAG, "T%d: Water refilled (basin=%d%%, reservoir=%d%%)", id,
           basin_level, reservoir_level);
  return ESP_OK;
}

esp_err_t climate_simulate_equipment_failure(uint8_t id, const char *equipment,
                                             bool failed) {
  if (id >= terrarium_count || !equipment)
    return ESP_ERR_INVALID_ARG;

  equipment_state_data_t *e = &terrariums[id].equipment;

  if (strcmp(equipment, "heating") == 0) {
    e->heating_error = failed;
    if (failed)
      e->heating_on = false;
  } else if (strcmp(equipment, "uv") == 0) {
    e->uv_lamp_error = failed;
    if (failed)
      e->uv_lamp_on = false;
  } else if (strcmp(equipment, "misting") == 0) {
    e->misting_error = failed;
    if (failed)
      e->misting_on = false;
  } else if (strcmp(equipment, "pump") == 0) {
    e->pump_error = failed;
    if (failed)
      e->pump_on = false;
  } else {
    return ESP_ERR_INVALID_ARG;
  }

  if (failed) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Panne équipement: %s", equipment);
    create_alert(id, ALERT_EQUIPMENT_FAILURE, ALERT_PRIORITY_CRITICAL, msg);
  }

  ESP_LOGW(TAG, "T%d: Equipment '%s' %s", id, equipment,
           failed ? "FAILED" : "REPAIRED");
  return ESP_OK;
}

void climate_set_comm_mode(comm_mode_t mode) {
  current_comm_mode = mode;
  ESP_LOGI(TAG, "Communication mode set to %d", mode);
}

comm_mode_t climate_get_comm_mode(void) { return current_comm_mode; }

esp_err_t climate_prepare_packet(uint8_t id, climate_packet_t *packet) {
  if (id >= terrarium_count || !packet)
    return ESP_ERR_INVALID_ARG;

  terrarium_config_t *t = &terrariums[id];

  packet->terrarium_id = id;
  time(&packet->timestamp);

  packet->temp_hot = t->sensors.temp_hot_zone;
  packet->temp_cold = t->sensors.temp_cold_zone;
  packet->humidity = t->sensors.humidity;
  packet->uv_index = t->sensors.uv_index;

  // Bitfield équipements
  packet->equipment_states = 0;
  if (t->equipment.heating_on)
    packet->equipment_states |= (1 << 0);
  if (t->equipment.uv_lamp_on)
    packet->equipment_states |= (1 << 1);
  if (t->equipment.day_light_on)
    packet->equipment_states |= (1 << 2);
  if (t->equipment.misting_on)
    packet->equipment_states |= (1 << 3);
  if (t->equipment.pump_on)
    packet->equipment_states |= (1 << 4);

  // Bitfield alertes
  packet->active_alerts = 0;
  for (int i = 0; i < alert_count; i++) {
    if (alerts[i].terrarium_id == id && alerts[i].active) {
      packet->active_alerts |= (1 << alerts[i].type);
    }
  }

  return ESP_OK;
}

esp_err_t climate_save_config(void) {
  nvs_handle_t handle;
  esp_err_t ret = nvs_open("climate", NVS_READWRITE, &handle);
  if (ret != ESP_OK)
    return ret;

  ret = nvs_set_u8(handle, "terra_count", terrarium_count);
  if (ret == ESP_OK) {
    ret = nvs_set_blob(handle, "terrariums", terrariums,
                       sizeof(terrarium_config_t) * terrarium_count);
  }

  if (ret == ESP_OK) {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Configuration saved (%d terrariums)", terrarium_count);
  }
  return ret;
}

esp_err_t climate_load_config(void) {
  nvs_handle_t handle;
  esp_err_t ret = nvs_open("climate", NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGI(TAG, "No saved configuration found");
      return ESP_OK;
    }
    return ret;
  }

  uint8_t count = 0;
  ret = nvs_get_u8(handle, "terra_count", &count);
  if (ret == ESP_OK && count > 0 && count <= MAX_TERRARIUMS) {
    size_t size = sizeof(terrarium_config_t) * count;
    ret = nvs_get_blob(handle, "terrariums", terrariums, &size);
    if (ret == ESP_OK) {
      terrarium_count = count;
      ESP_LOGI(TAG, "Configuration loaded (%d terrariums)", count);
    }
  }

  nvs_close(handle);
  return ret;
}

esp_err_t climate_export_history_csv(uint8_t id, const char *filepath) {
  if (id >= MAX_TERRARIUMS || !filepath)
    return ESP_ERR_INVALID_ARG;

  FILE *f = fopen(filepath, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file: %s", filepath);
    return ESP_ERR_INVALID_STATE;
  }

  // En-tête CSV
  fprintf(f, "Timestamp,TempHot,TempCold,Humidity,UVIndex\n");

  // Données
  history_point_t points[MAX_HISTORY_POINTS];
  uint16_t count = climate_get_history(id, points, MAX_HISTORY_POINTS);

  for (int i = 0; i < count; i++) {
    char time_str[32];
    struct tm *tm_info = localtime(&points[i].timestamp);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f, "%s,%.1f,%.1f,%.1f,%.2f\n", time_str, points[i].temp_hot,
            points[i].temp_cold, points[i].humidity, points[i].uv_index);
  }

  fclose(f);

  ESP_LOGI(TAG, "History exported to %s (%d points)", filepath, count);
  return ESP_OK;
}
