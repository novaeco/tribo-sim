#include "unity.h"

#include "ota_stream.h"

typedef struct {
    size_t lengths[32];
    size_t count;
    size_t total;
} chunk_record_t;

static esp_err_t record_chunk(const uint8_t *chunk, size_t len, void *ctx)
{
    chunk_record_t *rec = (chunk_record_t *)ctx;
    if (len == 0) {
        return ESP_OK;
    }
    TEST_ASSERT_TRUE(len <= 64);
    TEST_ASSERT_TRUE(rec->count < 32);
    rec->lengths[rec->count++] = len;
    rec->total += len;
    return ESP_OK;
}

TEST_CASE("ota_stream_chunks respects maximum block size", "[ota]")
{
    uint8_t data[500];
    for (size_t i = 0; i < sizeof(data); ++i) {
        data[i] = (uint8_t)i;
    }
    chunk_record_t rec = {0};
    TEST_ASSERT_EQUAL(ESP_OK, ota_stream_chunks(data, sizeof(data), 64, record_chunk, &rec));
    TEST_ASSERT_EQUAL_size_t(sizeof(data), rec.total);
    for (size_t i = 0; i < rec.count; ++i) {
        TEST_ASSERT_TRUE(rec.lengths[i] <= 64);
    }
}

