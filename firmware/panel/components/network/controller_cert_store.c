#include "controller_cert_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbedtls/x509_crt.h"

#include "root_ca.h"

#define TAG "cert_store"

#define CONTROLLER_CERT_NAMESPACE "panelcert"
#define CONTROLLER_CERT_KEY       "root_ca"
#define CONTROLLER_CERT_MAX_LEN   4096

static bool s_initialized = false;
static char *s_cert = NULL;
static size_t s_cert_len = 0;
static bool s_custom = false;

static esp_err_t validate_certificate_pem(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    size_t parse_len = len;
    if (data[len - 1] != '\0') {
        parse_len += 1;
    }
    int ret = mbedtls_x509_crt_parse(&crt, data, parse_len);
    mbedtls_x509_crt_free(&crt);
    if (ret != 0) {
        ESP_LOGE(TAG, "Invalid certificate (mbedtls err=%d)", ret);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void reset_cached_certificate(void)
{
    if (s_cert) {
        free(s_cert);
        s_cert = NULL;
    }
    s_cert_len = 0;
    s_custom = false;
}

static esp_err_t load_certificate_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONTROLLER_CERT_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No custom root CA stored yet");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace (%s)", esp_err_to_name(err));
        return err;
    }

    size_t required = 0;
    err = nvs_get_blob(handle, CONTROLLER_CERT_KEY, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Root CA blob missing; using built-in");
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to query root CA blob (%s)", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    if (required == 0 || required > CONTROLLER_CERT_MAX_LEN) {
        ESP_LOGW(TAG, "Stored certificate length %zu invalid; ignoring", required);
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buffer = malloc(required + 1);
    if (!buffer) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    memset(buffer, 0, required + 1);
    err = nvs_get_blob(handle, CONTROLLER_CERT_KEY, buffer, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load root CA blob (%s)", esp_err_to_name(err));
        free(buffer);
        return err;
    }

    size_t actual_len = strnlen(buffer, required);
    if (actual_len == required) {
        buffer[required] = '\0';
        actual_len = required;
    }

    err = validate_certificate_pem((const uint8_t *)buffer, actual_len + 1);
    if (err != ESP_OK) {
        free(buffer);
        return err;
    }

    reset_cached_certificate();
    s_cert = buffer;
    s_cert_len = actual_len;
    s_custom = true;
    ESP_LOGI(TAG, "Loaded custom root CA (%zu bytes)", actual_len);
    return ESP_OK;
}

esp_err_t controller_cert_store_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t err = load_certificate_from_nvs();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGW(TAG, "Failed to initialize certificate store (%s)", esp_err_to_name(err));
    }
    s_initialized = true;
    return ESP_OK;
}

bool controller_cert_store_is_ready(void)
{
    if (!s_initialized) {
        controller_cert_store_init();
    }
    return (controller_cert_store_get() != NULL);
}

bool controller_cert_store_has_custom(void)
{
    if (!s_initialized) {
        controller_cert_store_init();
    }
    return s_custom;
}

const char *controller_cert_store_get(void)
{
    if (!s_initialized) {
        controller_cert_store_init();
    }
    if (s_cert) {
        return s_cert;
    }
    return PANEL_CONTROLLER_ROOT_CA_PEM;
}

size_t controller_cert_store_length(void)
{
    if (!s_initialized) {
        controller_cert_store_init();
    }
    if (s_cert && s_cert_len > 0) {
        return s_cert_len;
    }
    return strlen(PANEL_CONTROLLER_ROOT_CA_PEM);
}

static esp_err_t store_blob_in_nvs(const void *data, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONTROLLER_CERT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace (%s)", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(handle, CONTROLLER_CERT_KEY, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store root CA blob (%s)", esp_err_to_name(err));
    }
    return err;
}

esp_err_t controller_cert_store_import(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > CONTROLLER_CERT_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        controller_cert_store_init();
    }
    size_t buffer_len = len;
    if (data[len - 1] != '\0') {
        buffer_len = len + 1;
    }
    char *buffer = malloc(buffer_len);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }
    memset(buffer, 0, buffer_len);
    memcpy(buffer, data, len);

    esp_err_t err = validate_certificate_pem((const uint8_t *)buffer, buffer_len);
    if (err != ESP_OK) {
        free(buffer);
        return err;
    }

    err = store_blob_in_nvs(buffer, buffer_len);
    if (err != ESP_OK) {
        free(buffer);
        return err;
    }

    reset_cached_certificate();
    s_cert = buffer;
    s_cert_len = strnlen(buffer, buffer_len - 1);
    s_custom = true;
    ESP_LOGI(TAG, "Custom root CA stored (%zu bytes)", s_cert_len);
    return ESP_OK;
}

esp_err_t controller_cert_store_import_from_file(const char *path)
{
    if (!path || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s (errno=%d)", path, errno);
        return ESP_FAIL;
    }
    char *buffer = malloc(CONTROLLER_CERT_MAX_LEN);
    if (!buffer) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t read = fread(buffer, 1, CONTROLLER_CERT_MAX_LEN - 1, f);
    fclose(f);
    if (read == 0) {
        free(buffer);
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[read] = '\0';
    esp_err_t err = controller_cert_store_import((const uint8_t *)buffer, read + 1);
    free(buffer);
    return err;
}

esp_err_t controller_cert_store_clear(void)
{
    if (!s_initialized) {
        controller_cert_store_init();
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONTROLLER_CERT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            reset_cached_certificate();
            return ESP_OK;
        }
        return err;
    }
    err = nvs_erase_key(handle, CONTROLLER_CERT_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        reset_cached_certificate();
    }
    return err;
}

