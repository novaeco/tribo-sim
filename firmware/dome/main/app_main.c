#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"

#include "include/config.h"
#include "include/regs.h"
#include "drivers/i2c_slave_if.h"
#include "drivers/ledc_cc.h"
#include "drivers/ws2812_rmt.h"
#include "drivers/fan_pwm.h"
#include "drivers/ntc_adc.h"
#include "drivers/uvi_sensor.h"

static const char *TAG = "DOME_APP";

static uint8_t regfile[256]; // I2C register space
static float t_c = 25.0f;
static volatile bool interlock_tripped = false;
static volatile uint32_t interlock_count = 0;

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    uint32_t bytes_written;
    uint8_t status;
    uint8_t error;
} dome_ota_ctx_t;

static dome_ota_ctx_t s_ota = {
    .handle = 0,
    .partition = NULL,
    .bytes_written = 0,
    .status = DOME_OTA_STATUS_IDLE,
    .error = 0,
};

static inline uint16_t rd16(uint8_t reg)
{
    return (uint16_t)regfile[reg] | ((uint16_t)regfile[reg + 1] << 8);
}

static inline void wr16(uint8_t reg, uint16_t value)
{
    regfile[reg] = (uint8_t)(value & 0xFF);
    regfile[reg + 1] = (uint8_t)(value >> 8);
}

static void dome_assert_int(bool assert)
{
    gpio_set_direction(DOME_INT_GPIO, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(DOME_INT_GPIO, assert ? 0 : 1); // open-drain: 0=assert, 1=release
}

static void dome_update_fan_registers(void)
{
    uint16_t pwm = fan_get_raw_pwm();
    regfile[DOME_REG_FAN_PWM_L] = (uint8_t)(pwm & 0xFF);
    regfile[DOME_REG_FAN_PWM_H] = (uint8_t)(pwm >> 8);

    uint8_t flags = 0;
    flags |= FAN_FLAG_PRESENT;
    if (fan_is_running()) {
        flags |= FAN_FLAG_RUNNING;
    }
    if (regfile[DOME_REG_STATUS] & ST_FAN_FAIL) {
        flags |= FAN_FLAG_ALARM;
    }
    regfile[DOME_REG_FAN_FLAGS] = flags;
}

static bool interlock_active(void)
{
    int level = gpio_get_level(DOME_INTERLOCK_GPIO);
    return level == 0; // active-low
}

static bool therm_hard_active(void)
{
#if DOME_THERM_GPIO >= 0
    return gpio_get_level(DOME_THERM_GPIO) == 0;
#else
    ESP_LOGW(TAG, "therm_hard_active() invoked but DOME_THERM_GPIO < 0");
    return false;
#endif
}

static void dome_ota_reset(void)
{
    if (s_ota.status == DOME_OTA_STATUS_BUSY && s_ota.partition) {
        esp_ota_end(s_ota.handle);
    }
    s_ota.partition = NULL;
    s_ota.handle = 0;
    s_ota.bytes_written = 0;
    s_ota.status = DOME_OTA_STATUS_IDLE;
    s_ota.error = 0;
    regfile[DOME_REG_OTA_STATUS] = s_ota.status;
    regfile[DOME_REG_OTA_ERROR] = s_ota.error;
    regfile[DOME_REG_OTA_CMD] = DOME_OTA_CMD_IDLE;
}

static void dome_ota_fail(esp_err_t err)
{
    ESP_LOGE(TAG, "OTA failure: %s", esp_err_to_name(err));
    if (s_ota.status == DOME_OTA_STATUS_BUSY) {
        esp_ota_end(s_ota.handle);
    }
    s_ota.status = DOME_OTA_STATUS_ERROR;
    s_ota.error = (uint8_t)(err & 0xFF);
    regfile[DOME_REG_OTA_STATUS] = s_ota.status;
    regfile[DOME_REG_OTA_ERROR] = s_ota.error;
    regfile[DOME_REG_OTA_CMD] = DOME_OTA_CMD_IDLE;
}

static void dome_ota_handle_data(const uint8_t *data, size_t len)
{
    if (s_ota.status != DOME_OTA_STATUS_BUSY) {
        return;
    }
    esp_err_t err = esp_ota_write(s_ota.handle, data, len);
    if (err != ESP_OK) {
        dome_ota_fail(err);
        return;
    }
    s_ota.bytes_written += len;
}

static void dome_ota_handle_command(uint8_t cmd)
{
    if (cmd == DOME_OTA_CMD_IDLE) {
        return;
    }
    if (cmd == DOME_OTA_CMD_BEGIN) {
        dome_ota_reset();
        s_ota.partition = esp_ota_get_next_update_partition(NULL);
        if (!s_ota.partition) {
            dome_ota_fail(ESP_ERR_NOT_FOUND);
            return;
        }
        esp_err_t err = esp_ota_begin(s_ota.partition, OTA_SIZE_UNKNOWN, &s_ota.handle);
        if (err != ESP_OK) {
            dome_ota_fail(err);
            return;
        }
        s_ota.status = DOME_OTA_STATUS_BUSY;
        s_ota.error = 0;
        regfile[DOME_REG_OTA_STATUS] = s_ota.status;
        regfile[DOME_REG_OTA_ERROR] = s_ota.error;
    } else if (cmd == DOME_OTA_CMD_WRITE) {
        // no-op: writes are handled immediately when data lands in buffer
    } else if (cmd == DOME_OTA_CMD_COMMIT) {
        if (s_ota.status != DOME_OTA_STATUS_BUSY) {
            return;
        }
        esp_err_t err = esp_ota_end(s_ota.handle);
        if (err != ESP_OK) {
            dome_ota_fail(err);
            return;
        }
        err = esp_ota_set_boot_partition(s_ota.partition);
        if (err != ESP_OK) {
            dome_ota_fail(err);
            return;
        }
        s_ota.status = DOME_OTA_STATUS_DONE;
        regfile[DOME_REG_OTA_STATUS] = s_ota.status;
        regfile[DOME_REG_OTA_ERROR] = 0;
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else if (cmd == DOME_OTA_CMD_ABORT) {
        dome_ota_reset();
    }
    regfile[DOME_REG_OTA_CMD] = DOME_OTA_CMD_IDLE;
}

static uint16_t encode_q8_16(float value, float min_value, float max_value)
{
    if (!isfinite(value)) {
        value = 0.0f;
    }
    if (value < min_value) {
        value = min_value;
    }
    if (value > max_value) {
        value = max_value;
    }
    return (uint16_t)lroundf(value * 256.0f);
}

static void dome_apply_outputs(bool force_uv_off)
{
    uint16_t cct_day = rd16(DOME_REG_CCT_DAY_L);
    uint16_t cct_warm = rd16(DOME_REG_CCT_WARM_L);
    uint16_t uva_set = rd16(DOME_REG_UVA_SET_L);
    uint16_t uva_clamp = rd16(DOME_REG_UVA_CLAMP_L);
    if (uva_clamp == 0) {
        uva_clamp = DOME_UVA_CLAMP_PM_DEFAULT;
    }

    uint8_t uvb_period = regfile[DOME_REG_UVB_PERIOD_S];
    uint8_t uvb_duty_b = regfile[DOME_REG_UVB_DUTY_PM];
    uint8_t uvb_clamp_b = regfile[DOME_REG_UVB_CLAMP_PM];

    uint16_t uvb_set_permille = (uint16_t)uvb_duty_b * 40u; // 0..10200
    if (uvb_set_permille > 10000) {
        uvb_set_permille = 10000;
    }
    uint16_t uvb_clamp = (uint16_t)uvb_clamp_b * 40u;
    if (uvb_clamp == 0) {
        uvb_clamp = DOME_UVB_CLAMP_PM_DEFAULT;
    }

    uint8_t status = regfile[DOME_REG_STATUS] & ~(ST_UVA_LIMIT | ST_UVB_LIMIT | ST_INTERLOCK | ST_THERM_HARD | ST_FAN_FAIL | ST_UVI_FAULT);

    bool interlock = force_uv_off || interlock_active();
    if (interlock) {
        status |= ST_INTERLOCK;
    }
    bool therm_hard = therm_hard_active();
    if (therm_hard) {
        status |= ST_THERM_HARD;
    }

    if (uva_set > uva_clamp) {
        uva_set = uva_clamp;
        status |= ST_UVA_LIMIT;
    }
    if (uvb_set_permille > uvb_clamp) {
        uvb_set_permille = uvb_clamp;
        status |= ST_UVB_LIMIT;
    }

    uint16_t uva_applied = interlock || therm_hard ? 0 : uva_set;
    uint16_t uvb_applied = interlock || therm_hard ? 0 : uvb_set_permille;

    ledc_cc_set(0, cct_day);
    ledc_cc_set(1, cct_warm);
    ledc_cc_set(2, uva_applied);
    ledc_cc_set(3, uvb_applied);

    esp_err_t uvi_err = uvi_sensor_init();
    if (uvi_err == ESP_OK) {
        esp_err_t poll_err = uvi_sensor_poll();
        if (poll_err != ESP_OK) {
            status |= ST_UVI_FAULT;
        }
    } else {
        status |= ST_UVI_FAULT;
    }

    uvi_sensor_measurement_t uvi_meas = {0};
    if (uvi_sensor_get(&uvi_meas) && uvi_meas.valid) {
        uint16_t irr_q8 = encode_q8_16(fmaxf(0.0f, uvi_meas.irradiance_uW_cm2), 0.0f, 255.0f);
        uint16_t uvi_q8 = encode_q8_16(fmaxf(0.0f, uvi_meas.uvi), 0.0f, 255.0f);
        wr16(DOME_REG_UVI_IRR_L, irr_q8);
        wr16(DOME_REG_UVI_INDEX_L, uvi_q8);
        if (uvi_meas.fault) {
            status |= ST_UVI_FAULT;
        }
    } else {
        wr16(DOME_REG_UVI_IRR_L, 0);
        wr16(DOME_REG_UVI_INDEX_L, 0);
        status |= ST_UVI_FAULT;
    }

    // Compute crude fan speed request based on heatsink temperature
    float fan_percent = 0.0f;
    if (t_c > 30.0f) {
        fan_percent = (t_c - 30.0f) * 20.0f; // +20% per °C above 30
        if (fan_percent > 100.0f) {
            fan_percent = 100.0f;
        }
    }
    fan_set_percent(fan_percent);
    if (fan_percent > 0.0f && !fan_is_running()) {
        status |= ST_FAN_FAIL;
    }

    regfile[DOME_REG_STATUS] = status;
    dome_update_fan_registers();

    // Update heat sink telemetry
    int8_t temp_deg = (int8_t)(t_c + (t_c >= 0 ? 0.5f : -0.5f));
    regfile[DOME_REG_TLM_T_HEAT] = (uint8_t)temp_deg;
    uint8_t tlm_flags = 0;
    if (interlock) {
        tlm_flags |= ST_INTERLOCK;
    }
    if (therm_hard) {
        tlm_flags |= ST_THERM_HARD;
    }
    regfile[DOME_REG_TLM_FLAGS] = tlm_flags;
}

static void IRAM_ATTR interlock_isr(void *arg)
{
    (void)arg;
    interlock_tripped = true;
    interlock_count++;
}

static void interlock_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << DOME_INTERLOCK_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(DOME_INTERLOCK_GPIO, interlock_isr, NULL);
}

static void therm_hard_init(void)
{
#if DOME_THERM_GPIO >= 0
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << DOME_THERM_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
#endif
}

static void dome_update_diagnostics(void)
{
    regfile[DOME_REG_DIAG_I2C_ERRORS] = 0; // placeholder for future error tracking
    uint32_t cnt = interlock_count;
    regfile[DOME_REG_DIAG_INT_COUNT_L] = (uint8_t)(cnt & 0xFF);
    regfile[DOME_REG_DIAG_INT_COUNT_H] = (uint8_t)((cnt >> 8) & 0xFF);
}

static bool range_intersects(uint8_t reg, size_t len, uint8_t base, size_t block_len)
{
    uint8_t end = reg + (uint8_t)(len ? (len - 1) : 0);
    uint8_t block_end = base + (uint8_t)(block_len ? (block_len - 1) : 0);
    return !(end < base || reg > block_end);
}

static void dome_handle_write(uint8_t reg, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }
    if ((size_t)reg + len > sizeof(regfile)) {
        len = sizeof(regfile) - reg;
    }
    memcpy(&regfile[reg], data, len);

    if (range_intersects(reg, len, DOME_REG_BLOCK_CCT, DOME_REG_BLOCK_CCT_LEN) ||
        range_intersects(reg, len, DOME_REG_BLOCK_UVA, DOME_REG_BLOCK_UVA_LEN) ||
        range_intersects(reg, len, DOME_REG_BLOCK_UVB, DOME_REG_BLOCK_UVB_LEN) ||
        range_intersects(reg, len, DOME_REG_SKY_CFG, 1)) {
        dome_apply_outputs(false);
    }

    if (range_intersects(reg, len, DOME_REG_BLOCK_OTA_DATA, DOME_REG_BLOCK_OTA_DATA_LEN)) {
        uint8_t block_start = DOME_REG_BLOCK_OTA_DATA;
        uint8_t block_end = block_start + DOME_REG_BLOCK_OTA_DATA_LEN - 1;
        uint8_t write_start = reg < block_start ? block_start : reg;
        uint8_t write_end = (reg + (uint8_t)(len - 1)) > block_end ? block_end : (uint8_t)(reg + len - 1);
        size_t chunk_len = (size_t)(write_end - write_start + 1);
        dome_ota_handle_data(&regfile[write_start], chunk_len);
    }

    if (range_intersects(reg, len, DOME_REG_BLOCK_OTA_CTRL, DOME_REG_BLOCK_OTA_CTRL_LEN)) {
        dome_ota_handle_command(regfile[DOME_REG_OTA_CMD]);
    }
}

static void telemetry_task(void *arg)
{
    (void)arg;
    while (1) {
        t_c = ntc_adc_read_celsius();
        if (t_c >= DOME_OT_SOFT_C) {
            regfile[DOME_REG_STATUS] |= ST_OT;
            dome_apply_outputs(true); // force UV off
        } else {
            regfile[DOME_REG_STATUS] &= ~ST_OT;
            dome_apply_outputs(false);
        }
        if (interlock_tripped) {
            interlock_tripped = false;
            dome_apply_outputs(true); // immediate cut
        }
        dome_update_diagnostics();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // INT open-drain default released
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << DOME_INT_GPIO,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    dome_assert_int(false);

    interlock_init();
    therm_hard_init();

    // LEDC channels
    ESP_ERROR_CHECK(ledc_cc_init());
    // Fan PWM
    fan_init(DOME_FAN_PWM);
    // WS2812
    ws2812_init(DOME_WS_GPIO);

    // I2C slave
    ESP_ERROR_CHECK(i2c_slave_if_init(I2C_NUM_0, DOME_I2C_SDA, DOME_I2C_SCL, DOME_I2C_ADDR));
    ESP_LOGI(TAG, "I2C slave ready @0x%02X", DOME_I2C_ADDR);

    // Default registers
    memset(regfile, 0, sizeof(regfile));
    wr16(DOME_REG_UVA_CLAMP_L, DOME_UVA_CLAMP_PM_DEFAULT);
    regfile[DOME_REG_UVB_CLAMP_PM] = (uint8_t)(DOME_UVB_CLAMP_PM_DEFAULT / 40);
    regfile[DOME_REG_UVB_PERIOD_S] = 60;
    regfile[DOME_REG_UVB_DUTY_PM] = 25; // 1000 permille approx
    regfile[DOME_REG_SKY_CFG] = 0;
    dome_ota_reset();
    dome_update_diagnostics();

    xTaskCreatePinnedToCore(telemetry_task, "telemetry", 4096, NULL, 6, NULL, 0);

    // Register protocol: reg pointer followed by optional payload
    while (1) {
        uint8_t buf[64];
        int n = i2c_slave_if_read(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n > 0) {
            uint8_t reg = buf[0];
            if (n == 1) {
                size_t available = sizeof(regfile) - reg;
                if (available > sizeof(buf)) {
                    available = sizeof(buf);
                }
                i2c_slave_if_write(&regfile[reg], available, pdMS_TO_TICKS(10));
            } else {
                size_t len = n - 1;
                dome_handle_write(reg, &buf[1], len);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
