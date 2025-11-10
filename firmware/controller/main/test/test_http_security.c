#include <stdio.h>

#include "unity.h"

#include "storage.h"
#include "net/credentials.h"

TEST_CASE("http authorization rejects missing token", "[http][security]")
{
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_erase());
    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_init());
    TEST_ASSERT_EQUAL(ESP_OK, credentials_init());

    const char *token = credentials_bootstrap_token();
    TEST_ASSERT_NOT_NULL(token);

    TEST_ASSERT_FALSE(credentials_authorize_bearer(NULL));
    TEST_ASSERT_FALSE(credentials_authorize_bearer("Bearer"));
    TEST_ASSERT_FALSE(credentials_authorize_bearer("Bearer   "));
    TEST_ASSERT_FALSE(credentials_authorize_bearer("Bearer invalid"));

    char header[160];
    snprintf(header, sizeof(header), "Bearer %s", token);
    TEST_ASSERT_TRUE(credentials_authorize_bearer(header));

    TEST_ASSERT_EQUAL(ESP_OK, storage_secure_deinit());
}
