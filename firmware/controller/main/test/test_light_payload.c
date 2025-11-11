#include "unity.h"

#include "cJSON.h"
#include "light_payload.h"

static void expect_parse_error(const char *json,
                               const char *field,
                               const char *detail)
{
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    light_payload_t payload = {0};
    char field_buf[32];
    char detail_buf[32];
    esp_err_t err = light_payload_parse(root, &payload, field_buf, sizeof(field_buf), detail_buf, sizeof(detail_buf));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_EQUAL_STRING(field, field_buf);
    TEST_ASSERT_EQUAL_STRING(detail, detail_buf);

    cJSON_Delete(root);
}

TEST_CASE("light payload rejects missing uvb period", "[http][light]")
{
    expect_parse_error("{\"cct\":{\"day\":100,\"warm\":200},\"uva\":{\"set\":10,\"clamp\":20},\"uvb\":{\"set\":1,\"clamp\":2}}",
                       "uvb.period_s",
                       "missing field");
}

TEST_CASE("light payload rejects non numeric duty", "[http][light]")
{
    expect_parse_error("{\"cct\":{\"day\":100,\"warm\":200},\"uva\":{\"set\":10,\"clamp\":20},\"uvb\":{\"set\":1,\"clamp\":2,\"period_s\":5,\"duty_pm\":\"bad\"}}",
                       "uvb.duty_pm",
                       "expected number");
}

TEST_CASE("light payload clamps duty and period", "[http][light]")
{
    const char *json = "{\"cct\":{\"day\":100,\"warm\":200},\"uva\":{\"set\":10,\"clamp\":20},\"uvb\":{\"set\":1,\"clamp\":2,\"period_s\":0,\"duty_pm\":20000}}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    light_payload_t payload = {0};
    char field_buf[32];
    char detail_buf[32];
    esp_err_t err = light_payload_parse(root, &payload, field_buf, sizeof(field_buf), detail_buf, sizeof(detail_buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT8(1, payload.uvb_period);
    TEST_ASSERT_EQUAL_FLOAT(10000.0f, payload.uvb_duty);

    cJSON_Delete(root);
}

TEST_CASE("light payload validates optional sky field", "[http][light]")
{
    expect_parse_error("{\"cct\":{\"day\":100,\"warm\":200},\"uva\":{\"set\":10,\"clamp\":20},\"uvb\":{\"set\":1,\"clamp\":2,\"period_s\":5,\"duty_pm\":50},\"sky\":\"blue\"}",
                       "sky",
                       "expected number");
}
