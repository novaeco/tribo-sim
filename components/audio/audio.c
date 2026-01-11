// Audio subsystem implementation
// Uses LEDC PWM for buzzer tone generation

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "audio.h"

static const char *TAG = "AUDIO";

// Configuration
#define AUDIO_GPIO          CONFIG_AUDIO_BUZZER_GPIO
#define AUDIO_LEDC_TIMER    LEDC_TIMER_1
#define AUDIO_LEDC_CHANNEL  LEDC_CHANNEL_1
#define AUDIO_LEDC_MODE     LEDC_LOW_SPEED_MODE

// Audio state
static bool s_audio_initialized = false;
static bool s_audio_enabled = true;
static uint8_t s_volume = 50;
static QueueHandle_t s_sound_queue = NULL;

// Note frequencies (Hz)
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_REST 0

// Note duration in ms
#define DUR_WHOLE    400
#define DUR_HALF     200
#define DUR_QUARTER  100
#define DUR_EIGHTH   50

// Sound sequence structure
typedef struct {
    uint16_t frequency;
    uint16_t duration_ms;
} Note;

// Sound sequences
static const Note sound_feed[] = {
    {NOTE_E5, DUR_EIGHTH}, {NOTE_G5, DUR_EIGHTH}, {NOTE_C5, DUR_QUARTER},
    {NOTE_REST, 0}
};

static const Note sound_play[] = {
    {NOTE_C5, DUR_EIGHTH}, {NOTE_E5, DUR_EIGHTH}, {NOTE_G5, DUR_EIGHTH},
    {NOTE_E5, DUR_EIGHTH}, {NOTE_C5, DUR_QUARTER},
    {NOTE_REST, 0}
};

static const Note sound_clean[] = {
    {NOTE_A5, DUR_EIGHTH}, {NOTE_REST, DUR_EIGHTH},
    {NOTE_A5, DUR_EIGHTH}, {NOTE_REST, DUR_EIGHTH},
    {NOTE_E5, DUR_QUARTER},
    {NOTE_REST, 0}
};

static const Note sound_heat_on[] = {
    {NOTE_C4, DUR_EIGHTH}, {NOTE_E4, DUR_QUARTER},
    {NOTE_REST, 0}
};

static const Note sound_heat_off[] = {
    {NOTE_E4, DUR_EIGHTH}, {NOTE_C4, DUR_QUARTER},
    {NOTE_REST, 0}
};

static const Note sound_happy[] = {
    {NOTE_C5, DUR_EIGHTH}, {NOTE_E5, DUR_EIGHTH}, {NOTE_G5, DUR_EIGHTH},
    {NOTE_C5, DUR_EIGHTH}, {NOTE_E5, DUR_EIGHTH}, {NOTE_G5, DUR_QUARTER},
    {NOTE_REST, 0}
};

static const Note sound_sad[] = {
    {NOTE_E4, DUR_QUARTER}, {NOTE_D4, DUR_QUARTER}, {NOTE_C4, DUR_HALF},
    {NOTE_REST, 0}
};

static const Note sound_sick[] = {
    {NOTE_A4, DUR_EIGHTH}, {NOTE_REST, DUR_EIGHTH},
    {NOTE_A4, DUR_EIGHTH}, {NOTE_REST, DUR_EIGHTH},
    {NOTE_A4, DUR_EIGHTH},
    {NOTE_REST, 0}
};

static const Note sound_hungry[] = {
    {NOTE_G4, DUR_QUARTER}, {NOTE_REST, DUR_EIGHTH},
    {NOTE_F4, DUR_QUARTER}, {NOTE_REST, DUR_EIGHTH},
    {NOTE_E4, DUR_HALF},
    {NOTE_REST, 0}
};

static const Note sound_sleep[] = {
    {NOTE_C5, DUR_HALF}, {NOTE_G4, DUR_HALF}, {NOTE_E4, DUR_WHOLE},
    {NOTE_REST, 0}
};

static const Note sound_wake[] = {
    {NOTE_E4, DUR_EIGHTH}, {NOTE_G4, DUR_EIGHTH}, {NOTE_C5, DUR_QUARTER},
    {NOTE_REST, 0}
};

static const Note sound_death[] = {
    {NOTE_C5, DUR_QUARTER}, {NOTE_B4, DUR_QUARTER},
    {NOTE_A4, DUR_QUARTER}, {NOTE_G4, DUR_QUARTER},
    {NOTE_F4, DUR_QUARTER}, {NOTE_E4, DUR_QUARTER},
    {NOTE_D4, DUR_QUARTER}, {NOTE_C4, DUR_WHOLE},
    {NOTE_REST, 0}
};

static const Note sound_button[] = {
    {NOTE_C5, DUR_EIGHTH},
    {NOTE_REST, 0}
};

static const Note sound_start[] = {
    {NOTE_C4, DUR_EIGHTH}, {NOTE_E4, DUR_EIGHTH}, {NOTE_G4, DUR_EIGHTH},
    {NOTE_C5, DUR_QUARTER}, {NOTE_G4, DUR_EIGHTH}, {NOTE_C5, DUR_HALF},
    {NOTE_REST, 0}
};

// Get sequence for effect
static const Note* get_sound_sequence(SoundEffect effect)
{
    switch (effect) {
        case SOUND_FEED:     return sound_feed;
        case SOUND_PLAY:     return sound_play;
        case SOUND_CLEAN:    return sound_clean;
        case SOUND_HEAT_ON:  return sound_heat_on;
        case SOUND_HEAT_OFF: return sound_heat_off;
        case SOUND_HAPPY:    return sound_happy;
        case SOUND_SAD:      return sound_sad;
        case SOUND_SICK:     return sound_sick;
        case SOUND_HUNGRY:   return sound_hungry;
        case SOUND_SLEEP:    return sound_sleep;
        case SOUND_WAKE:     return sound_wake;
        case SOUND_DEATH:    return sound_death;
        case SOUND_BUTTON:   return sound_button;
        case SOUND_START:    return sound_start;
        default:             return NULL;
    }
}

// Play a single tone
static void play_tone(uint16_t frequency, uint16_t duration_ms)
{
    if (frequency == NOTE_REST || frequency == 0) {
        ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);
        ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);
    } else {
        // Set frequency
        ledc_set_freq(AUDIO_LEDC_MODE, AUDIO_LEDC_TIMER, frequency);
        // Set duty based on volume (50% duty cycle max for square wave)
        uint32_t duty = (512 * s_volume) / 100;
        ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, duty);
        ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);
    }

    if (duration_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
    }
}

// Stop tone
static void stop_tone(void)
{
    ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);
    ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);
}

bool audio_init(void)
{
    if (s_audio_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing audio on GPIO %d", AUDIO_GPIO);

    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = AUDIO_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = AUDIO_LEDC_TIMER,
        .freq_hz = 1000,  // Initial frequency
        .clk_cfg = LEDC_AUTO_CLK
    };

    if (ledc_timer_config(&timer_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer");
        return false;
    }

    // Configure LEDC channel
    ledc_channel_config_t channel_conf = {
        .gpio_num = AUDIO_GPIO,
        .speed_mode = AUDIO_LEDC_MODE,
        .channel = AUDIO_LEDC_CHANNEL,
        .timer_sel = AUDIO_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };

    if (ledc_channel_config(&channel_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel");
        return false;
    }

    // Create sound queue
    s_sound_queue = xQueueCreate(8, sizeof(SoundEffect));
    if (!s_sound_queue) {
        ESP_LOGE(TAG, "Failed to create sound queue");
        return false;
    }

    s_audio_initialized = true;
    ESP_LOGI(TAG, "Audio initialized successfully");
    return true;
}

void audio_play(SoundEffect effect)
{
    if (!s_audio_initialized || !s_audio_enabled || effect == SOUND_NONE) {
        return;
    }

    xQueueSend(s_sound_queue, &effect, 0);
}

void audio_stop(void)
{
    // Clear queue
    if (s_sound_queue) {
        xQueueReset(s_sound_queue);
    }
    stop_tone();
}

void audio_set_volume(uint8_t volume)
{
    s_volume = volume > 100 ? 100 : volume;
}

uint8_t audio_get_volume(void)
{
    return s_volume;
}

void audio_set_enabled(bool enabled)
{
    s_audio_enabled = enabled;
    if (!enabled) {
        audio_stop();
    }
}

bool audio_is_enabled(void)
{
    return s_audio_enabled;
}

void audio_task(void *arg)
{
    (void)arg;

    if (!audio_init()) {
        ESP_LOGE(TAG, "Audio init failed, task exiting");
        vTaskDelete(NULL);
        return;
    }

    SoundEffect effect;

    while (1) {
        // Wait for sound to play
        if (xQueueReceive(s_sound_queue, &effect, portMAX_DELAY) == pdTRUE) {
            const Note *sequence = get_sound_sequence(effect);
            if (sequence) {
                // Play the sequence
                for (int i = 0; sequence[i].frequency != NOTE_REST || sequence[i].duration_ms != 0; i++) {
                    if (!s_audio_enabled) {
                        break;
                    }
                    play_tone(sequence[i].frequency, sequence[i].duration_ms);
                    // Small gap between notes
                    if (sequence[i].frequency != NOTE_REST) {
                        stop_tone();
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                }
                stop_tone();
            }
        }
    }
}
