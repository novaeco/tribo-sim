#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_check.h"

#include "mbedtls/sha256.h"

#include "include/config.h"
#include "include/regs.h"
#include "drivers/i2c_slave_if.h"
#include "drivers/ledc_cc.h"
#include "drivers/ws2812_rmt.h"
#include "drivers/fan_pwm.h"
#include "drivers/ntc_adc.h"
#include "drivers/uvi_sensor.h"

static const char *TAG = "DOME_APP";

#define OTA_VERSION_MAX_LEN 32

static uint8_t regfile[256]; // I2C register space
static float t_c = 25.0f;
static volatile bool interlock_tripped = false;
static volatile uint32_t interlock_count = 0;

#define UV_EVENT_MASK_UVA 0x01u
#define UV_EVENT_MASK_UVB 0x02u

static uint32_t s_uv_event_history[DOME_DIAG_UV_HISTORY_DEPTH] = {0};
static uint32_t s_uv_event_total = 0;
static uint8_t s_uv_event_head = 0;
static uint8_t s_uv_event_count = 0;
static bool s_uva_cut_active = false;
static bool s_uvb_cut_active = false;
static portMUX_TYPE s_uv_event_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    uint32_t bytes_written;
    uint32_t expected_size;
    uint8_t expected_sha[32];
    char expected_version[OTA_VERSION_MAX_LEN];
    bool expect_size;
    bool sha_active;
    uint8_t status;
    uint8_t error;
    mbedtls_sha256_context sha_ctx;
} dome_ota_ctx_t;

typedef struct {
    uint32_t magic;
    uint8_t state;
    uint8_t error;
    uint8_t flags;
    uint8_t reserved;
    uint32_t image_size;
    uint8_t sha256[32];
    char version[OTA_VERSION_MAX_LEN];
    char message[64];
} dome_ota_status_record_t;

typedef enum {
    DOME_OTA_STATE_IDLE = 0,
    DOME_OTA_STATE_MANIFEST_ACCEPTED = 1,
    DOME_OTA_STATE_DOWNLOADING = 2,
    DOME_OTA_STATE_VERIFYING = 3,
    DOME_OTA_STATE_READY = 4,
    DOME_OTA_STATE_PENDING_REBOOT = 5,
    DOME_OTA_STATE_SUCCESS = 6,
    DOME_OTA_STATE_FAILED = 7,
    DOME_OTA_STATE_ROLLED_BACK = 8,
} dome_ota_state_t;

#define DOME_OTA_STATE_MAGIC 0x444f4d45u

#define OTA_NVS_NAMESPACE "ota"
#define OTA_NVS_KEY "dome"

static dome_ota_ctx_t s_ota = {
    .handle = 0,
    .partition = NULL,
    .bytes_written = 0,
    .expected_size = 0,
    .expected_sha = {0},
    .expected_version = {0},
    .expect_size = false,
    .sha_active = false,
    .status = DOME_OTA_STATUS_IDLE,
    .error = 0,
};

static dome_ota_status_record_t s_ota_state = {
    .magic = DOME_OTA_STATE_MAGIC,
    .state = DOME_OTA_STATE_IDLE,
    .error = 0,
    .flags = 0,
    .image_size = 0,
    .sha256 = {0},
    .version = {0},
    .message = {0},
};

static uint8_t dome_hw_status_for(dome_ota_state_t state);
static void dome_status_sync_registers(void);
static void dome_status_store(void);
static void dome_status_commit(void);
static void dome_status_set_message(const char *message);
static void dome_status_set_manifest(uint32_t image_size, const uint8_t sha[32], const char *version);
static void dome_status_set(dome_ota_state_t state, uint8_t error, const char *message);
static void dome_status_load(void);
static void dome_status_handle_boot(void);
static int compare_versions(const char *current, const char *candidate);
static esp_err_t dome_ota_load_manifest(void);
static void dome_ota_reset_context(void);
static void dome_ota_fail(esp_err_t err, const char *message, uint8_t set_flags, uint8_t clear_flags);

static inline uint16_t rd16(uint8_t reg)
{
    return (uint16_t)regfile[reg] | ((uint16_t)regfile[reg + 1] << 8);
}

static inline void wr16(uint8_t reg, uint16_t value)
{
    regfile[reg] = (uint8_t)(value & 0xFF);
    regfile[reg + 1] = (uint8_t)(value >> 8);
}

static inline void wr32(uint8_t reg, uint32_t value)
{
    regfile[reg] = (uint8_t)(value & 0xFF);
    regfile[reg + 1] = (uint8_t)((value >> 8) & 0xFF);
    regfile[reg + 2] = (uint8_t)((value >> 16) & 0xFF);
    regfile[reg + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static inline uint32_t uv_event_encode(uint8_t channel_mask)
{
    uint32_t seconds = (uint32_t)((esp_timer_get_time() / 1000000ULL) & DOME_DIAG_UV_EVENT_TIMESTAMP_MASK);
    uint32_t encoded = seconds & DOME_DIAG_UV_EVENT_TIMESTAMP_MASK;
    if (channel_mask & UV_EVENT_MASK_UVA) {
        encoded |= DOME_DIAG_UV_EVENT_CH_UVA;
    }
    if (channel_mask & UV_EVENT_MASK_UVB) {
        encoded |= DOME_DIAG_UV_EVENT_CH_UVB;
    }
    return encoded;
}

static void uv_history_record(uint8_t channel_mask)
{
    if (channel_mask == 0) {
        return;
    }
    uint32_t encoded = uv_event_encode(channel_mask);
    portENTER_CRITICAL(&s_uv_event_lock);
    s_uv_event_history[s_uv_event_head] = encoded;
    s_uv_event_head = (uint8_t)((s_uv_event_head + 1) % DOME_DIAG_UV_HISTORY_DEPTH);
    if (s_uv_event_count < DOME_DIAG_UV_HISTORY_DEPTH) {
        ++s_uv_event_count;
    }
    if (s_uv_event_total != UINT32_MAX) {
        ++s_uv_event_total;
    }
    portEXIT_CRITICAL(&s_uv_event_lock);
}

static void uv_history_reset(void)
{
    portENTER_CRITICAL(&s_uv_event_lock);
    memset(s_uv_event_history, 0, sizeof(s_uv_event_history));
    s_uv_event_head = 0;
    s_uv_event_count = 0;
    s_uv_event_total = 0;
    s_uva_cut_active = false;
    s_uvb_cut_active = false;
    portEXIT_CRITICAL(&s_uv_event_lock);
}

static void dome_reg_write_message(const char *message)
{
    memset(&regfile[DOME_REG_OTA_STATUS_MSG], 0, DOME_REG_OTA_STATUS_MSG_LEN);
    if (message && message[0]) {
        strncpy((char *)&regfile[DOME_REG_OTA_STATUS_MSG], message, DOME_REG_OTA_STATUS_MSG_LEN - 1);
    }
}

static uint8_t dome_hw_status_for(dome_ota_state_t state)
{
    switch (state) {
    case DOME_OTA_STATE_IDLE:
    case DOME_OTA_STATE_MANIFEST_ACCEPTED:
        return DOME_OTA_STATUS_IDLE;
    case DOME_OTA_STATE_DOWNLOADING:
    case DOME_OTA_STATE_VERIFYING:
        return DOME_OTA_STATUS_BUSY;
    case DOME_OTA_STATE_READY:
    case DOME_OTA_STATE_PENDING_REBOOT:
    case DOME_OTA_STATE_SUCCESS:
        return DOME_OTA_STATUS_DONE;
    case DOME_OTA_STATE_FAILED:
    case DOME_OTA_STATE_ROLLED_BACK:
    default:
        return DOME_OTA_STATUS_ERROR;
    }
}

static void dome_status_sync_registers(void)
{
    regfile[DOME_REG_OTA_STATUS] = dome_hw_status_for((dome_ota_state_t)s_ota_state.state);
    regfile[DOME_REG_OTA_ERROR] = s_ota_state.error;
    regfile[DOME_REG_OTA_FLAGS] = s_ota_state.flags;
    wr32(DOME_REG_OTA_EXPECTED_SIZE_L, s_ota_state.image_size);
    memcpy(&regfile[DOME_REG_OTA_EXPECTED_SHA], s_ota_state.sha256, 32);
    memset(&regfile[DOME_REG_OTA_VERSION], 0, DOME_REG_OTA_VERSION_LEN);
    strncpy((char *)&regfile[DOME_REG_OTA_VERSION], s_ota_state.version, DOME_REG_OTA_VERSION_LEN - 1);
    dome_reg_write_message(s_ota_state.message);
}

static void dome_status_store(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed: %s", OTA_NVS_NAMESPACE, esp_err_to_name(err));
        return;
    }
    s_ota_state.magic = DOME_OTA_STATE_MAGIC;
    esp_err_t set_err = nvs_set_blob(handle, OTA_NVS_KEY, &s_ota_state, sizeof(s_ota_state));
    if (set_err == ESP_OK) {
        set_err = nvs_commit(handle);
    }
    if (set_err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_blob(%s) failed: %s", OTA_NVS_KEY, esp_err_to_name(set_err));
    }
    nvs_close(handle);
}

static void dome_status_commit(void)
{
    dome_status_sync_registers();
    dome_status_store();
}

static void dome_status_set_message(const char *message)
{
    if (message) {
        strncpy(s_ota_state.message, message, sizeof(s_ota_state.message));
        s_ota_state.message[sizeof(s_ota_state.message) - 1] = '\0';
    }
}

static void dome_status_set_manifest(uint32_t image_size, const uint8_t sha[32], const char *version)
{
    s_ota_state.image_size = image_size;
    if (sha) {
        memcpy(s_ota_state.sha256, sha, 32);
    }
    if (version) {
        strncpy(s_ota_state.version, version, sizeof(s_ota_state.version));
        s_ota_state.version[sizeof(s_ota_state.version) - 1] = '\0';
    }
}

static void dome_status_set(dome_ota_state_t state, uint8_t error, const char *message)
{
    s_ota_state.state = (uint8_t)state;
    s_ota_state.error = error;
    if (message) {
        dome_status_set_message(message);
    }
    dome_status_commit();
}

static void dome_status_load(void)
{
    size_t required = sizeof(s_ota_state);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_blob(handle, OTA_NVS_KEY, &s_ota_state, &required);
        nvs_close(handle);
    }
    if (err != ESP_OK || required != sizeof(s_ota_state) || s_ota_state.magic != DOME_OTA_STATE_MAGIC) {
        ESP_LOGI(TAG, "OTA status reset (err=%s, required=%zu)", esp_err_to_name(err), required);
        memset(&s_ota_state, 0, sizeof(s_ota_state));
        s_ota_state.magic = DOME_OTA_STATE_MAGIC;
        s_ota_state.state = DOME_OTA_STATE_IDLE;
    }
    s_ota_state.message[sizeof(s_ota_state.message) - 1] = '\0';
    s_ota_state.version[sizeof(s_ota_state.version) - 1] = '\0';
    dome_status_sync_registers();
}

static const char *advance_to_digit(const char *s)
{
    while (*s && !isdigit((unsigned char)*s)) {
        ++s;
    }
    return s;
}

static long read_version_component(const char **s)
{
    const char *p = advance_to_digit(*s);
    if (!*p) {
        *s = p;
        return 0;
    }
    char *end = NULL;
    long value = strtol(p, &end, 10);
    if (end == p) {
        *s = p;
        return 0;
    }
    if (*end == '.' || *end == '-' || *end == '+') {
        *s = end + 1;
    } else {
        *s = end;
    }
    return value;
}

static int compare_versions(const char *current, const char *candidate)
{
    if (!current || !candidate) {
        return 0;
    }
    const char *cur = current;
    const char *cand = candidate;
    for (int i = 0; i < 4; ++i) {
        long cur_v = read_version_component(&cur);
        long cand_v = read_version_component(&cand);
        if (cand_v > cur_v) {
            return 1;
        }
        if (cand_v < cur_v) {
            return -1;
        }
        if ((*cur == '\0' && *cand == '\0') || (!isdigit((unsigned char)*cur) && !isdigit((unsigned char)*cand))) {
            break;
        }
    }
    return 0;
}

static void dome_ota_reset_context(void)
{
    if (s_ota.handle) {
        esp_ota_abort(s_ota.handle);
        s_ota.handle = 0;
    }
    if (s_ota.sha_active) {
        mbedtls_sha256_free(&s_ota.sha_ctx);
        s_ota.sha_active = false;
    }
    s_ota.partition = NULL;
    s_ota.bytes_written = 0;
    s_ota.expected_size = 0;
    memset(s_ota.expected_sha, 0, sizeof(s_ota.expected_sha));
    memset(s_ota.expected_version, 0, sizeof(s_ota.expected_version));
    s_ota.expect_size = false;
    s_ota.status = DOME_OTA_STATUS_IDLE;
    s_ota.error = 0;
}

static esp_err_t dome_ota_load_manifest(void)
{
    uint32_t expected_size = (uint32_t)regfile[DOME_REG_OTA_EXPECTED_SIZE_L] |
                             ((uint32_t)regfile[DOME_REG_OTA_EXPECTED_SIZE_L + 1] << 8) |
                             ((uint32_t)regfile[DOME_REG_OTA_EXPECTED_SIZE_L + 2] << 16) |
                             ((uint32_t)regfile[DOME_REG_OTA_EXPECTED_SIZE_L + 3] << 24);

    uint8_t expected_sha[32];
    memcpy(expected_sha, &regfile[DOME_REG_OTA_EXPECTED_SHA], sizeof(expected_sha));

    char version_buf[OTA_VERSION_MAX_LEN];
    memset(version_buf, 0, sizeof(version_buf));
    memcpy(version_buf, &regfile[DOME_REG_OTA_VERSION],
           DOME_REG_OTA_VERSION_LEN < OTA_VERSION_MAX_LEN ? DOME_REG_OTA_VERSION_LEN : OTA_VERSION_MAX_LEN - 1);
    version_buf[OTA_VERSION_MAX_LEN - 1] = '\0';

    size_t version_len = strnlen(version_buf, sizeof(version_buf));
    if (version_len == 0) {
        ESP_LOGE(TAG, "Manifest version vide");
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < version_len; ++i) {
        if (!isprint((unsigned char)version_buf[i])) {
            ESP_LOGE(TAG, "Manifest version invalide (caractère 0x%02x)", (unsigned char)version_buf[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }

    char message_buf[DOME_REG_OTA_STATUS_MSG_LEN + 1];
    memset(message_buf, 0, sizeof(message_buf));
    memcpy(message_buf, &regfile[DOME_REG_OTA_STATUS_MSG], DOME_REG_OTA_STATUS_MSG_LEN);
    message_buf[DOME_REG_OTA_STATUS_MSG_LEN] = '\0';

    s_ota.expected_size = expected_size;
    s_ota.expect_size = expected_size != 0;
    memcpy(s_ota.expected_sha, expected_sha, sizeof(expected_sha));
    strncpy(s_ota.expected_version, version_buf, sizeof(s_ota.expected_version));
    s_ota.expected_version[sizeof(s_ota.expected_version) - 1] = '\0';
    s_ota.bytes_written = 0;

    s_ota_state.flags &= ~(DOME_OTA_FLAG_HASH_OK | DOME_OTA_FLAG_HASH_FAIL | DOME_OTA_FLAG_APPLIED | DOME_OTA_FLAG_ROLLBACK);
    s_ota_state.flags |= DOME_OTA_FLAG_META_READY;
    dome_status_set_manifest(expected_size, expected_sha, version_buf);
    if (message_buf[0]) {
        dome_status_set_message(message_buf);
    }
    s_ota_state.error = 0;
    s_ota_state.state = DOME_OTA_STATE_MANIFEST_ACCEPTED;
    dome_status_commit();

    ESP_LOGI(TAG, "Manifest chargé (taille=%u, version=%s)", expected_size, s_ota.expected_version);
    return ESP_OK;
}

static void dome_ota_fail(esp_err_t err, const char *message, uint8_t set_flags, uint8_t clear_flags)
{
    ESP_LOGE(TAG, "OTA failure: %s", esp_err_to_name(err));
    dome_ota_reset_context();
    s_ota_state.flags &= ~(clear_flags | DOME_OTA_FLAG_APPLIED);
    s_ota_state.flags |= set_flags;
    dome_status_set(DOME_OTA_STATE_FAILED, (uint8_t)(err & 0xFF), message ? message : "OTA échouée");
    regfile[DOME_REG_OTA_CMD] = DOME_OTA_CMD_IDLE;
}

static void dome_status_handle_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "Partition active introuvable");
        return;
    }

    esp_app_desc_t running_desc = {0};
    esp_err_t err = esp_ota_get_partition_description(running, &running_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lecture description appli: %s", esp_err_to_name(err));
        return;
    }

    esp_ota_img_states_t img_state = ESP_OTA_IMG_UNDEFINED;
    err = esp_ota_get_state_partition(running, &img_state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_get_state_partition: %s", esp_err_to_name(err));
        img_state = ESP_OTA_IMG_UNDEFINED;
    }

    bool have_expected = s_ota_state.version[0] != '\0';
    bool version_match = have_expected &&
                         strncmp(s_ota_state.version, running_desc.version, sizeof(running_desc.version)) == 0;

    if (img_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (!have_expected || version_match) {
            dome_status_set(DOME_OTA_STATE_VERIFYING, 0, "Auto-test en cours");
            esp_err_t mark = esp_ota_mark_app_valid_cancel_rollback();
            if (mark == ESP_OK) {
                s_ota_state.flags |= DOME_OTA_FLAG_APPLIED;
                dome_status_set_manifest(s_ota_state.image_size, s_ota_state.sha256, running_desc.version);
                s_ota_state.flags &= ~DOME_OTA_FLAG_ROLLBACK;
                dome_status_set(DOME_OTA_STATE_SUCCESS, 0, "OTA validée");
            } else {
                ESP_LOGE(TAG, "esp_ota_mark_app_valid_cancel_rollback échoue: %s", esp_err_to_name(mark));
                s_ota_state.flags &= ~DOME_OTA_FLAG_APPLIED;
                dome_status_set(DOME_OTA_STATE_FAILED, (uint8_t)(mark & 0xFF), "Validation OTA échouée");
            }
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "Rollback détecté (%s)", running_desc.version);
            s_ota_state.flags |= DOME_OTA_FLAG_ROLLBACK;
            dome_status_set(DOME_OTA_STATE_ROLLED_BACK, 0, msg);
        }
        return;
    }

    if (have_expected && !version_match) {
        if (compare_versions(s_ota_state.version, running_desc.version) > 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Rollback vers %s", running_desc.version);
            s_ota_state.flags |= DOME_OTA_FLAG_ROLLBACK;
            dome_status_set(DOME_OTA_STATE_ROLLED_BACK, 0, msg);
        } else {
            dome_status_set_manifest(s_ota_state.image_size, s_ota_state.sha256, running_desc.version);
            s_ota_state.flags |= DOME_OTA_FLAG_APPLIED;
            s_ota_state.flags &= ~DOME_OTA_FLAG_ROLLBACK;
            dome_status_set(DOME_OTA_STATE_SUCCESS, 0, "Version active mise à jour");
        }
        return;
    }

    if (img_state == ESP_OTA_IMG_VALID) {
        if (!(s_ota_state.flags & DOME_OTA_FLAG_APPLIED)) {
            s_ota_state.flags |= DOME_OTA_FLAG_APPLIED;
        }
        if (s_ota_state.state == DOME_OTA_STATE_PENDING_REBOOT ||
            s_ota_state.state == DOME_OTA_STATE_VERIFYING ||
            s_ota_state.state == DOME_OTA_STATE_MANIFEST_ACCEPTED) {
            s_ota_state.flags &= ~DOME_OTA_FLAG_ROLLBACK;
            dome_status_set(DOME_OTA_STATE_SUCCESS, 0, "OTA validée");
            return;
        }
    }

    dome_status_commit();
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

static void dome_ota_handle_data(const uint8_t *data, size_t len)
{
    if (s_ota_state.state != DOME_OTA_STATE_DOWNLOADING || !s_ota.handle || len == 0) {
        return;
    }
    esp_err_t err = esp_ota_write(s_ota.handle, data, len);
    if (err != ESP_OK) {
        dome_ota_fail(err, "Écriture OTA échouée", 0, DOME_OTA_FLAG_HASH_OK);
        return;
    }
    if (s_ota.sha_active) {
        mbedtls_sha256_update(&s_ota.sha_ctx, data, len);
    }
    s_ota.bytes_written += len;
    if (s_ota.expect_size && s_ota.bytes_written > s_ota.expected_size) {
        dome_ota_fail(ESP_ERR_INVALID_SIZE, "Taille manifeste dépassée", DOME_OTA_FLAG_HASH_FAIL, DOME_OTA_FLAG_HASH_OK);
    }
}

static void dome_ota_handle_command(uint8_t cmd)
{
    if (cmd == DOME_OTA_CMD_IDLE) {
        return;
    }
    if (cmd == DOME_OTA_CMD_BEGIN) {
        if (!(regfile[DOME_REG_OTA_FLAGS] & DOME_OTA_FLAG_META_READY)) {
            dome_ota_fail(ESP_ERR_INVALID_STATE, "Manifest absent", 0, DOME_OTA_FLAG_HASH_OK | DOME_OTA_FLAG_HASH_FAIL);
            return;
        }
        esp_err_t err = dome_ota_load_manifest();
        if (err != ESP_OK) {
            dome_ota_fail(err, "Manifest invalide", DOME_OTA_FLAG_HASH_FAIL, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        dome_ota_reset_context();
        s_ota.partition = esp_ota_get_next_update_partition(NULL);
        if (!s_ota.partition) {
            dome_ota_fail(ESP_ERR_NOT_FOUND, "Partition OTA introuvable", 0, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        err = esp_ota_begin(s_ota.partition, OTA_SIZE_UNKNOWN, &s_ota.handle);
        if (err != ESP_OK) {
            dome_ota_fail(err, "esp_ota_begin échoue", 0, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        mbedtls_sha256_init(&s_ota.sha_ctx);
        mbedtls_sha256_starts(&s_ota.sha_ctx, 0);
        s_ota.sha_active = true;
        s_ota.bytes_written = 0;
        dome_status_set(DOME_OTA_STATE_DOWNLOADING, 0, "Réception en cours");
    } else if (cmd == DOME_OTA_CMD_WRITE) {
        // no-op
    } else if (cmd == DOME_OTA_CMD_COMMIT) {
        if (!s_ota.handle) {
            dome_ota_fail(ESP_ERR_INVALID_STATE, "OTA non initialisée", 0, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        uint8_t digest[32];
        if (s_ota.sha_active) {
            mbedtls_sha256_finish(&s_ota.sha_ctx, digest);
            mbedtls_sha256_free(&s_ota.sha_ctx);
            s_ota.sha_active = false;
        } else {
            memset(digest, 0, sizeof(digest));
        }
        if (memcmp(digest, s_ota.expected_sha, sizeof(digest)) != 0) {
            dome_ota_fail(ESP_ERR_INVALID_CRC, "Hash SHA-256 invalide", DOME_OTA_FLAG_HASH_FAIL, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        if (s_ota.expect_size && s_ota.bytes_written != s_ota.expected_size) {
            dome_ota_fail(ESP_ERR_INVALID_SIZE, "Taille inattendue", DOME_OTA_FLAG_HASH_FAIL, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        esp_err_t err = esp_ota_end(s_ota.handle);
        s_ota.handle = 0;
        if (err != ESP_OK) {
            dome_ota_fail(err, "esp_ota_end échoue", 0, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        esp_app_desc_t new_desc = {0};
        err = esp_ota_get_image_desc(s_ota.partition, &new_desc);
        if (err != ESP_OK) {
            dome_ota_fail(err, "Lecture desc image échoue", 0, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        if (strncmp(new_desc.version, s_ota.expected_version, sizeof(new_desc.version)) != 0) {
            dome_ota_fail(ESP_ERR_INVALID_RESPONSE, "Version manifest ≠ binaire", DOME_OTA_FLAG_HASH_FAIL, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (running) {
            esp_app_desc_t running_desc = {0};
            if (esp_ota_get_partition_description(running, &running_desc) == ESP_OK) {
                if (compare_versions(running_desc.version, new_desc.version) >= 0) {
                    dome_ota_fail(ESP_ERR_INVALID_STATE, "Version non monotone", DOME_OTA_FLAG_HASH_FAIL, DOME_OTA_FLAG_HASH_OK);
                    return;
                }
            }
        }
        s_ota_state.flags |= DOME_OTA_FLAG_HASH_OK;
        s_ota_state.flags &= ~DOME_OTA_FLAG_HASH_FAIL;
        s_ota_state.flags &= ~DOME_OTA_FLAG_ROLLBACK;
        dome_status_set(DOME_OTA_STATE_READY, 0, "Image validée");

        err = esp_ota_set_boot_partition(s_ota.partition);
        if (err != ESP_OK) {
            dome_ota_fail(err, "Sélection partition échoue", 0, DOME_OTA_FLAG_HASH_OK);
            return;
        }
        s_ota_state.flags |= DOME_OTA_FLAG_APPLIED;
        dome_status_set(DOME_OTA_STATE_PENDING_REBOOT, 0, "Redémarrage requis");
        dome_ota_reset_context();
    } else if (cmd == DOME_OTA_CMD_ABORT) {
        dome_ota_reset_context();
        s_ota_state.flags &= ~(DOME_OTA_FLAG_HASH_OK | DOME_OTA_FLAG_HASH_FAIL | DOME_OTA_FLAG_APPLIED);
        dome_status_set(DOME_OTA_STATE_FAILED, 0, "OTA annulée");
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

    bool uva_cut = (uva_set > 0) && (interlock || therm_hard);
    bool uvb_cut = (uvb_set_permille > 0) && (interlock || therm_hard);
    bool prev_uva_cut;
    bool prev_uvb_cut;
    portENTER_CRITICAL(&s_uv_event_lock);
    prev_uva_cut = s_uva_cut_active;
    prev_uvb_cut = s_uvb_cut_active;
    s_uva_cut_active = uva_cut;
    s_uvb_cut_active = uvb_cut;
    portEXIT_CRITICAL(&s_uv_event_lock);

    uint8_t event_mask = 0;
    if (uva_cut && !prev_uva_cut) {
        event_mask |= UV_EVENT_MASK_UVA;
    }
    if (uvb_cut && !prev_uvb_cut) {
        event_mask |= UV_EVENT_MASK_UVB;
    }
    if (event_mask != 0) {
        uv_history_record(event_mask);
    }

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
    uint32_t history[DOME_DIAG_UV_HISTORY_DEPTH];
    uint8_t head;
    uint32_t total;

    portENTER_CRITICAL(&s_uv_event_lock);
    memcpy(history, s_uv_event_history, sizeof(history));
    head = s_uv_event_head;
    total = s_uv_event_total;
    portEXIT_CRITICAL(&s_uv_event_lock);

    uint32_t i2c_errors = i2c_slave_if_get_error_count();
    if (i2c_errors > 0xFFFFu) {
        i2c_errors = 0xFFFFu;
    }
    wr16(DOME_REG_DIAG_I2C_ERR_L, (uint16_t)i2c_errors);

    uint32_t pwm_errors = fan_get_error_count();
    if (pwm_errors > 0xFFFFu) {
        pwm_errors = 0xFFFFu;
    }
    wr16(DOME_REG_DIAG_PWM_ERR_L, (uint16_t)pwm_errors);

    uint32_t cnt = interlock_count;
    if (cnt > 0xFFFFu) {
        cnt = 0xFFFFu;
    }
    wr16(DOME_REG_DIAG_INT_COUNT_L, (uint16_t)cnt);

    uint32_t total_clamped = total;
    if (total_clamped > 0xFFu) {
        total_clamped = 0xFFu;
    }
    regfile[DOME_REG_DIAG_UV_EVENT_COUNT] = (uint8_t)total_clamped;
    regfile[DOME_REG_DIAG_UV_EVENT_HEAD] = head;
    regfile[DOME_REG_DIAG_CMD] = DOME_DIAG_CMD_NONE;

    for (size_t i = 0; i < DOME_DIAG_UV_HISTORY_DEPTH; ++i) {
        uint8_t offset = (uint8_t)(DOME_REG_DIAG_UV_HISTORY + i * DOME_DIAG_UV_EVENT_STRIDE);
        wr32(offset, history[i]);
    }
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

    bool message_changed = range_intersects(reg, len, DOME_REG_OTA_STATUS_MSG, DOME_REG_OTA_STATUS_MSG_LEN);
    bool flags_changed = range_intersects(reg, len, DOME_REG_OTA_FLAGS, 1);

    if (message_changed) {
        char msg_buf[DOME_REG_OTA_STATUS_MSG_LEN + 1];
        memset(msg_buf, 0, sizeof(msg_buf));
        memcpy(msg_buf, &regfile[DOME_REG_OTA_STATUS_MSG], DOME_REG_OTA_STATUS_MSG_LEN);
        dome_status_set_message(msg_buf);
    }
    if (flags_changed) {
        s_ota_state.flags = regfile[DOME_REG_OTA_FLAGS];
    }
    if (message_changed || flags_changed) {
        dome_status_commit();
    }

    if (range_intersects(reg, len, DOME_REG_BLOCK_OTA_CTRL, DOME_REG_BLOCK_OTA_CTRL_LEN)) {
        dome_ota_handle_command(regfile[DOME_REG_OTA_CMD]);
    }

    if (range_intersects(reg, len, DOME_REG_DIAG_CMD, 1)) {
        uint8_t cmd = regfile[DOME_REG_DIAG_CMD];
        if (cmd == DOME_DIAG_CMD_RESET) {
            i2c_slave_if_reset_errors();
            fan_reset_error_count();
            interlock_count = 0;
            uv_history_reset();
            regfile[DOME_REG_DIAG_UV_EVENT_COUNT] = 0;
            regfile[DOME_REG_DIAG_UV_EVENT_HEAD] = 0;
        }
        regfile[DOME_REG_DIAG_CMD] = DOME_DIAG_CMD_NONE;
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
    uv_history_reset();
    wr16(DOME_REG_UVA_CLAMP_L, DOME_UVA_CLAMP_PM_DEFAULT);
    regfile[DOME_REG_UVB_CLAMP_PM] = (uint8_t)(DOME_UVB_CLAMP_PM_DEFAULT / 40);
    regfile[DOME_REG_UVB_PERIOD_S] = 60;
    regfile[DOME_REG_UVB_DUTY_PM] = 25; // 1000 permille approx
    regfile[DOME_REG_SKY_CFG] = 0;
    dome_status_load();
    dome_status_handle_boot();
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
