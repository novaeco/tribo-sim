#include "ws2812_rmt.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
static const char* TAG="WS2812";

// WS2812 timing @ 800kHz
// T0H=0.4us, T0L=0.85us ; T1H=0.8us, T1L=0.45us
#define RMT_RES_HZ (10*1000*1000) // 0.1us per tick
static rmt_channel_handle_t tx_chan;
static rmt_encoder_handle_t bytes_encoder;
static rmt_encoder_handle_t copy_encoder;

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes_encoder;
    rmt_symbol_word_t reset_symbol;
} ws2812_encoder_t;

static size_t rmt_encode_ws2812(ws2812_encoder_t *enc, rmt_channel_handle_t channel,
                                const void *primary_data, size_t data_size,
                                rmt_encode_state_t *ret_state)
{
    // First encode GRB bytes
    rmt_encode_state_t state = 0;
    size_t encoded = enc->bytes_encoder->encode(enc->bytes_encoder, channel, primary_data, data_size, &state);
    if (state & RMT_ENCODING_COMPLETE) {
        // then reset (low for >50us), here ~80us
        rmt_write_symbol(channel, &enc->reset_symbol, 1);
        *ret_state = RMT_ENCODING_COMPLETE;
    } else {
        *ret_state = state;
    }
    return encoded;
}

static esp_err_t rmt_del_ws2812_encoder(ws2812_encoder_t *enc)
{
    if (enc->bytes_encoder) rmt_del_encoder(enc->bytes_encoder);
    free(enc);
    return ESP_OK;
}

static esp_err_t rmt_new_ws2812_encoder(rmt_encoder_handle_t *ret_encoder)
{
    ws2812_encoder_t *enc = calloc(1, sizeof(ws2812_encoder_t));
    enc->base.encode = (rmt_encode_function_t)rmt_encode_ws2812;
    enc->base.del = (rmt_encoder_del_function_t)rmt_del_ws2812_encoder;

    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = { .duration0 = 4,  .level0 = 1, .duration1 = 9,  .level1 = 0 }, // 0.4us H, 0.9us L
        .bit1 = { .duration0 = 8,  .level0 = 1, .duration1 = 5,  .level1 = 0 }, // 0.8us H, 0.5us L
        .flags = {
            .msb_first = 1
        }
    };
    rmt_new_bytes_encoder(&bytes_cfg, &enc->bytes_encoder);
    // Reset 80us low
    enc->reset_symbol = (rmt_symbol_word_t){ .level0=0, .duration0=800, .level1=0, .duration1=0 };
    *ret_encoder = &enc->base;
    return ESP_OK;
}

static rmt_encoder_handle_t ws_encoder;

void ws2812_init(int gpio){
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .mem_block_symbols = 128,
        .resolution_hz = RMT_RES_HZ,
        .trans_queue_depth = 4
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &tx_chan));
    ESP_ERROR_CHECK(rmt_enable(tx_chan));
    ESP_ERROR_CHECK(rmt_new_ws2812_encoder(&ws_encoder));
}

void ws2812_write_rgb(uint8_t r, uint8_t g, uint8_t b){
    uint8_t grb[3] = {g, r, b};
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    rmt_transmit(tx_chan, ws_encoder, grb, sizeof(grb), &tx_cfg);
    rmt_tx_wait_all_done(tx_chan, -1);
}
