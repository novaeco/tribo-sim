#include "credentials.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/platform_util.h"
#include "storage.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"

#define TAG "credentials"

#define SECRETS_NAMESPACE "secrets"
#define KEY_CERT "server_cert"
#define KEY_KEY "server_key"
#define KEY_CERT_EXPIRY "cert_not_after"
#define KEY_TOKEN_HASH "token_hash"
#define KEY_TOKEN_SALT "token_salt"

#define TOKEN_BYTES 32
#define TOKEN_SALT_BYTES 16
#define CERT_VALIDITY_DAYS 180
#define CERT_RENEW_MARGIN_SECONDS (30 * 24 * 3600)

static char *s_cert_pem = NULL;
static size_t s_cert_len = 0;
static char *s_key_pem = NULL;
static size_t s_key_len = 0;
static uint8_t s_token_hash[32];
static uint8_t s_token_salt[TOKEN_SALT_BYTES];
static bool s_token_ready = false;
static bool s_initialised = false;
static char s_bootstrap_token[(TOKEN_BYTES * 2) + 1];
static bool s_bootstrap_available = false;
static uint64_t s_cert_not_after = 0;

static bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

static void hex_encode(const uint8_t *in, size_t len, char *out)
{
    static const char *k_hex = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = k_hex[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = k_hex[in[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

static void utc_time_string(time_t t, char buffer[16])
{
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    strftime(buffer, 16, "%Y%m%d%H%M%S", &tm_buf);
}

static esp_err_t allocate_and_load_blob(nvs_handle_t handle, const char *key, char **buffer, size_t *len)
{
    size_t required = 0;
    esp_err_t err = nvs_get_blob(handle, key, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *buffer = NULL;
        *len = 0;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_get_blob(%s) query failed", key);
    if (required == 0) {
        *buffer = NULL;
        *len = 0;
        return ESP_OK;
    }
    char *tmp = malloc(required);
    ESP_GOTO_ON_FALSE(tmp, ESP_ERR_NO_MEM, exit, TAG, "malloc failed for key %s", key);
    err = nvs_get_blob(handle, key, tmp, &required);
    if (err != ESP_OK) {
        free(tmp);
        ESP_RETURN_ON_ERROR(err, TAG, "nvs_get_blob(%s) load failed", key);
    }
    *buffer = tmp;
    *len = required;
    return ESP_OK;
exit:
    return ESP_ERR_NO_MEM;
}

static esp_err_t commit_blob(nvs_handle_t handle, const char *key, const void *data, size_t len)
{
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle, key, data, len), TAG, "nvs_set_blob(%s) failed", key);
    return ESP_OK;
}

static esp_err_t generate_token(nvs_handle_t handle, bool force)
{
    if (!force && s_token_ready) {
        return ESP_OK;
    }
    uint8_t raw[TOKEN_BYTES];
    uint8_t salt[TOKEN_SALT_BYTES];
    esp_fill_random(raw, sizeof(raw));
    esp_fill_random(salt, sizeof(salt));

    hex_encode(raw, sizeof(raw), s_bootstrap_token);
    s_bootstrap_available = true;

    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    ESP_GOTO_ON_ERROR(mbedtls_sha256_starts_ret(&ctx, 0), fail, TAG, "sha256 start failed");
    ESP_GOTO_ON_ERROR(mbedtls_sha256_update_ret(&ctx, salt, sizeof(salt)), fail, TAG, "sha256 update salt failed");
    ESP_GOTO_ON_ERROR(mbedtls_sha256_update_ret(&ctx, (const unsigned char *)s_bootstrap_token, strlen(s_bootstrap_token)), fail, TAG, "sha256 update token failed");
    ESP_GOTO_ON_ERROR(mbedtls_sha256_finish_ret(&ctx, digest), fail, TAG, "sha256 finish failed");
    mbedtls_sha256_free(&ctx);

    ESP_RETURN_ON_ERROR(commit_blob(handle, KEY_TOKEN_HASH, digest, sizeof(digest)), TAG, "store token hash failed");
    ESP_RETURN_ON_ERROR(commit_blob(handle, KEY_TOKEN_SALT, salt, sizeof(salt)), TAG, "store token salt failed");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "nvs_commit token failed");

    memcpy(s_token_hash, digest, sizeof(digest));
    memcpy(s_token_salt, salt, sizeof(salt));
    s_token_ready = true;
    mbedtls_platform_zeroize(raw, sizeof(raw));
    mbedtls_platform_zeroize(salt, sizeof(salt));
    mbedtls_platform_zeroize(digest, sizeof(digest));
    ESP_LOGI(TAG, "Provisioned new API bearer token");
    return ESP_OK;

fail:
    mbedtls_platform_zeroize(raw, sizeof(raw));
    mbedtls_platform_zeroize(salt, sizeof(salt));
    mbedtls_platform_zeroize(digest, sizeof(digest));
    mbedtls_sha256_free(&ctx);
    return ESP_FAIL;
}

static esp_err_t generate_certificate(nvs_handle_t handle)
{
    esp_err_t ret = ESP_FAIL;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "terrarium-cert";

    mbedtls_pk_init(&key);
    mbedtls_x509write_cert_init(&crt);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    int mret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));
    ESP_GOTO_ON_FALSE(mret == 0, ESP_FAIL, exit, TAG, "ctr_drbg_seed failed (%d)", mret);

    mret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    ESP_GOTO_ON_FALSE(mret == 0, ESP_FAIL, exit, TAG, "pk_setup failed (%d)", mret);

    mret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);
    ESP_GOTO_ON_FALSE(mret == 0, ESP_FAIL, exit, TAG, "rsa_gen_key failed (%d)", mret);

    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

    uint8_t serial_bytes[16];
    esp_fill_random(serial_bytes, sizeof(serial_bytes));
    mbedtls_mpi serial;
    mbedtls_mpi_init(&serial);
    mbedtls_mpi_read_binary(&serial, serial_bytes, sizeof(serial_bytes));
    mbedtls_x509write_crt_set_serial(&crt, &serial);

    mbedtls_x509write_crt_set_subject_name(&crt, "CN=terrarium-controller,O=Tribo,OU=Terrarium");
    mbedtls_x509write_crt_set_issuer_name(&crt, "CN=terrarium-controller,O=Tribo,OU=Terrarium");

    time_t now = time(NULL);
    if (now < 1577836800) { // 2020-01-01
        now = 1577836800;
    }
    time_t not_before = now - 3600;
    time_t not_after = now + (CERT_VALIDITY_DAYS * 24 * 3600);
    char nb_str[16];
    char na_str[16];
    utc_time_string(not_before, nb_str);
    utc_time_string(not_after, na_str);
    mbedtls_x509write_crt_set_validity(&crt, nb_str, na_str);

    unsigned char cert_buf[4096];
    unsigned char key_buf[2048];
    memset(cert_buf, 0, sizeof(cert_buf));
    memset(key_buf, 0, sizeof(key_buf));

    mret = mbedtls_x509write_crt_pem(&crt, cert_buf, sizeof(cert_buf), mbedtls_ctr_drbg_random, &ctr_drbg);
    ESP_GOTO_ON_FALSE(mret == 0, ESP_FAIL, exit, TAG, "crt_pem failed (%d)", mret);

    mret = mbedtls_pk_write_key_pem(&key, key_buf, sizeof(key_buf));
    ESP_GOTO_ON_FALSE(mret == 0, ESP_FAIL, exit, TAG, "write_key_pem failed (%d)", mret);

    size_t cert_len = strlen((char *)cert_buf) + 1;
    size_t key_len = strlen((char *)key_buf) + 1;

    ESP_RETURN_ON_ERROR(commit_blob(handle, KEY_CERT, cert_buf, cert_len), TAG, "store cert failed");
    ESP_RETURN_ON_ERROR(commit_blob(handle, KEY_KEY, key_buf, key_len), TAG, "store key failed");
    ESP_RETURN_ON_ERROR(nvs_set_u64(handle, KEY_CERT_EXPIRY, (uint64_t)not_after), TAG, "store cert expiry failed");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit cert failed");

    char *new_cert = malloc(cert_len);
    char *new_key = malloc(key_len);
    ESP_GOTO_ON_FALSE(new_cert && new_key, ESP_ERR_NO_MEM, exit, TAG, "malloc credential buffers failed");
    memcpy(new_cert, cert_buf, cert_len);
    memcpy(new_key, key_buf, key_len);
    free(s_cert_pem);
    free(s_key_pem);
    s_cert_pem = new_cert;
    s_key_pem = new_key;
    s_cert_len = cert_len;
    s_key_len = key_len;
    s_cert_not_after = not_after;

    ESP_LOGI(TAG, "Provisioned new TLS certificate (valid %d days)", CERT_VALIDITY_DAYS);
    ret = ESP_OK;

exit:
    mbedtls_platform_zeroize(serial_bytes, sizeof(serial_bytes));
    mbedtls_platform_zeroize(cert_buf, sizeof(cert_buf));
    mbedtls_platform_zeroize(key_buf, sizeof(key_buf));
    mbedtls_mpi_free(&serial);
    mbedtls_pk_free(&key);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate TLS credentials");
    }
    return ret;
}

static bool certificate_needs_rotation(uint64_t not_after)
{
    time_t now = time(NULL);
    if (now <= 0) {
        return false;
    }
    if (not_after == 0) {
        return true;
    }
    if (now + CERT_RENEW_MARGIN_SECONDS >= (time_t)not_after) {
        return true;
    }
    return false;
}

static esp_err_t load_or_generate_certificate(nvs_handle_t handle, bool force)
{
    if (s_cert_pem && s_key_pem && !force) {
        if (certificate_needs_rotation(s_cert_not_after)) {
            return generate_certificate(handle);
        }
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(allocate_and_load_blob(handle, KEY_CERT, &s_cert_pem, &s_cert_len), TAG, "load cert failed");
    ESP_RETURN_ON_ERROR(allocate_and_load_blob(handle, KEY_KEY, &s_key_pem, &s_key_len), TAG, "load key failed");
    esp_err_t err = nvs_get_u64(handle, KEY_CERT_EXPIRY, &s_cert_not_after);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_cert_not_after = 0;
    } else if (err != ESP_OK) {
        ESP_RETURN_ON_ERROR(err, TAG, "load cert expiry failed");
    }

    if (!s_cert_pem || !s_key_pem || force || certificate_needs_rotation(s_cert_not_after)) {
        return generate_certificate(handle);
    }
    return ESP_OK;
}

static esp_err_t load_or_generate_token(nvs_handle_t handle, bool force)
{
    if (s_token_ready && !force) {
        return ESP_OK;
    }
    size_t required = sizeof(s_token_hash);
    esp_err_t err = nvs_get_blob(handle, KEY_TOKEN_HASH, s_token_hash, &required);
    if (err == ESP_OK && required == sizeof(s_token_hash)) {
        required = sizeof(s_token_salt);
        err = nvs_get_blob(handle, KEY_TOKEN_SALT, s_token_salt, &required);
        if (err == ESP_OK && required == sizeof(s_token_salt) && !force) {
            s_token_ready = true;
            return ESP_OK;
        }
    }
    return generate_token(handle, true);
}

esp_err_t credentials_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SECRETS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_RETURN_ON_ERROR(storage_secure_init(), TAG, "storage init failed");
        err = nvs_open(SECRETS_NAMESPACE, NVS_READWRITE, &handle);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open(%s) failed", SECRETS_NAMESPACE);

    ESP_GOTO_ON_ERROR(load_or_generate_certificate(handle, false), cleanup, TAG, "certificate provisioning failed");
    ESP_GOTO_ON_ERROR(load_or_generate_token(handle, false), cleanup, TAG, "token provisioning failed");
    nvs_close(handle);
    s_initialised = true;
    return ESP_OK;

cleanup:
    nvs_close(handle);
    return err;
}

const uint8_t *credentials_server_cert(size_t *len)
{
    if (len) {
        *len = s_cert_len;
    }
    return (const uint8_t *)s_cert_pem;
}

const uint8_t *credentials_server_key(size_t *len)
{
    if (len) {
        *len = s_key_len;
    }
    return (const uint8_t *)s_key_pem;
}

bool credentials_bootstrap_token_available(void)
{
    return s_bootstrap_available;
}

const char *credentials_bootstrap_token(void)
{
    if (s_bootstrap_available) {
        s_bootstrap_available = false;
        return s_bootstrap_token;
    }
    return NULL;
}

static bool verify_token(const char *token, size_t len)
{
    if (!s_token_ready || !token || len == 0) {
        return false;
    }
    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_update_ret(&ctx, s_token_salt, sizeof(s_token_salt)) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_update_ret(&ctx, (const unsigned char *)token, len) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_finish_ret(&ctx, digest) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);
    bool valid = constant_time_equal(digest, s_token_hash, sizeof(digest));
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return valid;
}

bool credentials_authorize_bearer(const char *authorization_header)
{
    if (!authorization_header) {
        return false;
    }
    while (*authorization_header == ' ') {
        ++authorization_header;
    }
    if (strncasecmp(authorization_header, "Bearer", 6) != 0) {
        return false;
    }
    authorization_header += 6;
    while (*authorization_header == ' ') {
        ++authorization_header;
    }
    size_t len = strlen(authorization_header);
    if (len == 0 || len > 128) {
        return false;
    }
    return verify_token(authorization_header, len);
}

esp_err_t credentials_rotate(bool rotate_cert, bool rotate_token)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SECRETS_NAMESPACE, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open(%s) failed", SECRETS_NAMESPACE);
    if (rotate_cert) {
        ESP_GOTO_ON_ERROR(load_or_generate_certificate(handle, true), cleanup, TAG, "cert rotation failed");
    }
    if (rotate_token) {
        ESP_GOTO_ON_ERROR(load_or_generate_token(handle, true), cleanup, TAG, "token rotation failed");
    }
    nvs_close(handle);
    return ESP_OK;
cleanup:
    nvs_close(handle);
    return err;
}
