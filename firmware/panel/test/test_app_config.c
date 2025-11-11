#include "unity.h"

#include <stdbool.h>
#include <string.h>

#include "app_config.h"

typedef struct {
    esp_err_t open_status;
    esp_err_t first_get_status;
    esp_err_t second_get_status;
    size_t required_length;
    app_config_t stored_cfg;
    bool close_called;
} fake_nvs_ctx_t;

static fake_nvs_ctx_t s_fake_ctx;

static esp_err_t fake_nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out_handle)
{
    (void)name;
    (void)mode;
    if (out_handle) {
        *out_handle = (nvs_handle_t)0x1;
    }
    return s_fake_ctx.open_status;
}

static void fake_nvs_close(nvs_handle_t handle)
{
    (void)handle;
    s_fake_ctx.close_called = true;
}

static esp_err_t fake_nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
    (void)handle;
    (void)key;
    if (!length) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!out_value) {
        *length = s_fake_ctx.required_length;
        return s_fake_ctx.first_get_status;
    }

    if (s_fake_ctx.second_get_status == ESP_OK) {
        size_t copy_len = *length;
        if (copy_len > sizeof(app_config_t)) {
            copy_len = sizeof(app_config_t);
        }
        memcpy(out_value, &s_fake_ctx.stored_cfg, copy_len);
    }
    return s_fake_ctx.second_get_status;
}

void setUp(void)
{
    memset(&s_fake_ctx, 0, sizeof(s_fake_ctx));
    s_fake_ctx.open_status = ESP_OK;
    s_fake_ctx.first_get_status = ESP_OK;
    s_fake_ctx.second_get_status = ESP_OK;
    s_fake_ctx.required_length = sizeof(app_config_t);
    app_config_use_custom_nvs_ops(NULL);
}

void tearDown(void)
{
    app_config_use_custom_nvs_ops(NULL);
}

static void use_fake_nvs(void)
{
    app_config_nvs_ops_t ops = {
        .open = fake_nvs_open,
        .close = fake_nvs_close,
        .get_blob = fake_nvs_get_blob,
    };
    app_config_use_custom_nvs_ops(&ops);
}

TEST_CASE("app_config_load returns defaults when blob missing", "[app_config]")
{
    use_fake_nvs();
    s_fake_ctx.first_get_status = ESP_ERR_NVS_NOT_FOUND;

    app_config_t cfg;
    esp_err_t err = app_config_load(&cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

    app_config_t defaults;
    app_config_get_defaults(&defaults);
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &cfg, sizeof(app_config_t));
    TEST_ASSERT_TRUE(s_fake_ctx.close_called);
}

TEST_CASE("app_config_load rolls back to defaults on read failure", "[app_config]")
{
    use_fake_nvs();
    s_fake_ctx.second_get_status = ESP_FAIL;

    app_config_t cfg;
    esp_err_t err = app_config_load(&cfg);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);

    app_config_t defaults;
    app_config_get_defaults(&defaults);
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &cfg, sizeof(app_config_t));
    TEST_ASSERT_TRUE(s_fake_ctx.close_called);
}

TEST_CASE("app_config_load surfaces open errors", "[app_config]")
{
    use_fake_nvs();
    s_fake_ctx.open_status = ESP_ERR_NVS_NOT_INITIALIZED;

    app_config_t cfg;
    esp_err_t err = app_config_load(&cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_INITIALIZED, err);

    app_config_t defaults;
    app_config_get_defaults(&defaults);
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &cfg, sizeof(app_config_t));
    TEST_ASSERT_FALSE(s_fake_ctx.close_called);
}

