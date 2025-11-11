#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "cJSON.h"

typedef struct {
    uint16_t cct_day;
    uint16_t cct_warm;
    uint16_t uva_set;
    uint16_t uva_clamp;
    float uvb_set;
    float uvb_clamp;
    uint8_t uvb_period;
    float uvb_duty;
    bool has_sky;
    uint8_t sky_value;
} light_payload_t;

esp_err_t light_payload_parse(cJSON *root,
                              light_payload_t *out,
                              char *field_buf,
                              size_t field_buf_len,
                              char *detail_buf,
                              size_t detail_buf_len);
