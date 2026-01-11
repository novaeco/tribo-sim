// Icon sprite data for LVGL
// 32x32 pixel icons in RGB565 format

#include "lvgl.h"

// Color definitions
#define C_TRANS   0x0000
#define C_WHITE   0xFFFF
#define C_BLACK   0x0000
#define C_ORANGE  0xFD20
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_PINK    0xF81F
#define C_CYAN    0x07FF
#define C_BROWN   0x8200

// Food icon (32x32) - Apple/fruit shape
static const uint16_t icon_food_data[32 * 32] = {
    // Simplified - filled with orange color pattern
    [0 ... 1023] = C_ORANGE
};

const lv_image_dsc_t img_icon_food = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 32,
        .h = 32,
    },
    .data_size = 32 * 32 * 2,
    .data = (const uint8_t *)icon_food_data,
};

// Heat icon (32x32) - Flame shape
static const uint16_t icon_heat_data[32 * 32] = {
    [0 ... 1023] = C_RED
};

const lv_image_dsc_t img_icon_heat = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 32,
        .h = 32,
    },
    .data_size = 32 * 32 * 2,
    .data = (const uint8_t *)icon_heat_data,
};

// Play icon (32x32) - Ball/toy shape
static const uint16_t icon_play_data[32 * 32] = {
    [0 ... 1023] = C_PINK
};

const lv_image_dsc_t img_icon_play = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 32,
        .h = 32,
    },
    .data_size = 32 * 32 * 2,
    .data = (const uint8_t *)icon_play_data,
};

// Clean icon (32x32) - Broom/sparkle shape
static const uint16_t icon_clean_data[32 * 32] = {
    [0 ... 1023] = C_CYAN
};

const lv_image_dsc_t img_icon_clean = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 32,
        .h = 32,
    },
    .data_size = 32 * 32 * 2,
    .data = (const uint8_t *)icon_clean_data,
};

// Sleep icon (32x32) - Moon/Zzz shape
static const uint16_t icon_sleep_data[32 * 32] = {
    [0 ... 1023] = C_BLUE
};

const lv_image_dsc_t img_icon_sleep = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 32,
        .h = 32,
    },
    .data_size = 32 * 32 * 2,
    .data = (const uint8_t *)icon_sleep_data,
};

// Heart icon (32x32)
static const uint16_t icon_heart_data[32 * 32] = {
    [0 ... 1023] = C_RED
};

const lv_image_dsc_t img_icon_heart = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 32,
        .h = 32,
    },
    .data_size = 32 * 32 * 2,
    .data = (const uint8_t *)icon_heart_data,
};

// Terrarium background tile (64x64)
static const uint16_t terrarium_bg_data[64 * 64] = {
    [0 ... 4095] = 0x2104  // Dark green
};

const lv_image_dsc_t img_terrarium_bg = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 64,
        .h = 64,
    },
    .data_size = 64 * 64 * 2,
    .data = (const uint8_t *)terrarium_bg_data,
};
