/**
 * @file main.c
 * @brief ESP32-P4 LVGL Smart Panel for GUITION JC4880P443C
 *
 * Features:
 *   - Multi-page UI with navigation
 *   - Status bar with WiFi, Bluetooth, Date, Time
 *   - SD Card mounted with image loading support
 *   - PNG/JPEG decoder for LVGL
 *   - Touch support (GT911)
 *   - WiFi preparation via esp_hosted
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// GPIO and power control
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"

// I2C for touch
#include "driver/i2c_master.h"

// SD Card (SDMMC mode - Slot 0 available on 7" board)
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

// MIPI-DSI and LCD panel
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_types.h"

// Touch driver
#include "esp_lcd_touch_gt911.h"

// WiFi via ESP32-C6 (esp_hosted)
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

// SNTP for real-time clock
#include "esp_sntp.h"
#include <sys/time.h>
#include <time.h>

// LVGL port
#include "esp_lvgl_port.h"
#include "lvgl.h"

// Bluetooth via ESP32-C6 (esp_hosted) - conditionally included
#if CONFIG_BT_ENABLED
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_hosted_bluedroid.h"
#endif // CONFIG_BT_ENABLED

// ESP-Hosted (always needed for WiFi)
#include "esp_hosted.h"

// Tribolonotus Pet Simulator
#include "tribolonotus_types.h"
#include "pet_simulator.h"
#include "ui_pet.h"

// ESP32-C6 OTA firmware - REMOVED (update already done to v2.8.5)

static const char *TAG = "SMART_PANEL";
static const char *WIFI_TAG = "WIFI";

// ====================================================================================
// HARDWARE CONFIGURATION - JC1060P470C (7-inch 1024x600 JD9165BA)
// ====================================================================================

// Display resolution (7-inch IPS)
#define LCD_H_RES 1024
#define LCD_V_RES 600

#define LCD_RST_GPIO 5
#define LCD_BL_GPIO 23

// Touch I2C (same as 4.3")
#define TOUCH_I2C_SDA 7
#define TOUCH_I2C_SCL 8
#define TOUCH_I2C_FREQ_HZ 400000

// MIPI-DSI Configuration for JD9165BA (2 lanes)
#define DSI_LANE_NUM 2
#define DSI_LANE_BITRATE 800 // Increased for 1024x600@60Hz
#define DPI_CLOCK_MHZ 52     // ~51.2 MHz as per datasheet

#define DSI_PHY_LDO_CHANNEL 3
#define DSI_PHY_VOLTAGE_MV 2500

#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_PWM_FREQ 5000

// SD Card GPIOs - SDMMC Mode (uses Slot 0, ESP-Hosted uses Slot 1)
// Note: On 7" board, SDMMC Slot 0 is available for SD card
#define SD_CMD_GPIO 44
#define SD_CLK_GPIO 43
#define SD_D0_GPIO 39
#define SD_D1_GPIO 40
#define SD_D2_GPIO 41
#define SD_D3_GPIO 42

#define SD_MOUNT_POINT "/sdcard"

// ====================================================================================
// AUDIO ES8311 CODEC CONFIG (Official ESP32-P4 pin mapping from ESP-IDF
// example)
// ====================================================================================
#define AUDIO_ENABLED 1 // Test esp_codec_dev approach

// ES8311 I2C control pins (from ESP-IDF i2s_es8311 example for ESP32-P4)
#define ES8311_I2C_SDA GPIO_NUM_7 // I2C Data
#define ES8311_I2C_SCL GPIO_NUM_8 // I2C Clock
#define ES8311_I2C_ADDR 0x18      // ES8311 default I2C address

// I2S pins for ES8311 (Official ESP32-P4 defaults from Kconfig.projbuild)
#define I2S_MCLK_GPIO GPIO_NUM_13 // I2S Master Clock
#define I2S_BCK_GPIO GPIO_NUM_12  // I2S Bit Clock
#define I2S_WS_GPIO GPIO_NUM_10   // I2S Word Select (LRCK)
#define I2S_DO_GPIO GPIO_NUM_9    // I2S Data Out (to ES8311 SDIN)
#define I2S_DI_GPIO GPIO_NUM_11   // I2S Data In (from ES8311 mic, optional)

// PA (Power Amplifier NS4150B) enable pin
#define PA_ENABLE_GPIO GPIO_NUM_53 // Power Amp control (high = enabled)

// Audio settings
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_MCLK_MULTIPLE 384 // MCLK = SAMPLE_RATE * 384
#define AUDIO_VOLUME 60         // Volume 0-100

// Sound frequencies (Hz) for UI tones
#define SOUND_CLICK_FREQ 1000
#define SOUND_SUCCESS_FREQ 1500
#define SOUND_ERROR_FREQ 400
#define SOUND_ALERT_FREQ 2000

// ====================================================================================
// BATTERY CONFIG (Optional - set BATTERY_ENABLED to 0 if no fuel gauge)
// ====================================================================================
#define BATTERY_ENABLED 0   // Set to 1 if battery monitoring available
#define BATTERY_SIMULATED 1 // Use simulated battery level for demo

// ====================================================================================
// JD9165BA INIT COMMANDS (7-inch 1024x600 panel)
// Based on MTK_JD9165BA_HKC7.0_IPS datasheet
// Using named arrays to avoid linker compound literal issues
// ====================================================================================

// Command data arrays for JD9165BA
static const uint8_t jd_cmd_0[] = {0x00};
static const uint8_t jd_cmd_1[] = {0x49, 0x61, 0x02, 0x00};
static const uint8_t jd_cmd_2[] = {0x01};
static const uint8_t jd_cmd_3[] = {0x0C};
static const uint8_t jd_cmd_4[] = {0x00};
static const uint8_t jd_cmd_5[] = {0x11};
static const uint8_t jd_cmd_6[] = {0x04};
static const uint8_t jd_cmd_7[] = {0x05};
static const uint8_t jd_cmd_8[] = {0x19};
static const uint8_t jd_cmd_9[] = {0x18};
static const uint8_t jd_cmd_10[] = {0x02};
static const uint8_t jd_cmd_11[] = {0x22};
static const uint8_t jd_cmd_12[] = {0x12};
static const uint8_t jd_cmd_13[] = {0x64};
static const uint8_t jd_cmd_14[] = {0x08};
static const uint8_t jd_cmd_15[] = {0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11,
                                    0x10, 0x06, 0x08, 0x1F, 0x1D};
static const uint8_t jd_cmd_16[] = {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
                                    0x0D, 0x0D, 0x0D, 0x0D, 0x0D};
static const uint8_t jd_cmd_17[] = {0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11,
                                    0x10, 0x07, 0x09, 0x1E, 0x1C};
static const uint8_t jd_cmd_18[] = {0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11,
                                    0x10, 0x1C, 0x1E, 0x09, 0x07};
static const uint8_t jd_cmd_19[] = {0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11,
                                    0x10, 0x1D, 0x1F, 0x08, 0x06};
static const uint8_t jd_cmd_20[] = {0x00, 0x00, 0x11, 0x11};
static const uint8_t jd_cmd_21[] = {0x99};
static const uint8_t jd_cmd_22[] = {0x06};
static const uint8_t jd_cmd_23[] = {0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35,
                                    0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29};
static const uint8_t jd_cmd_24[] = {0x0A};
static const uint8_t jd_cmd_25[] = {0x4F};
static const uint8_t jd_cmd_26[] = {0x40};
static const uint8_t jd_cmd_27[] = {0x3E};
static const uint8_t jd_cmd_28[] = {0x78};
static const uint8_t jd_cmd_29[] = {0x0D};
static const uint8_t jd_cmd_30[] = {0x0C};

static const st7701_lcd_init_cmd_t jd9165ba_lcd_cmds[] = {
    // Page select 0
    {0x30, jd_cmd_0, 1, 0},
    {0xF7, jd_cmd_1, 4, 0},
    // Page select 1
    {0x30, jd_cmd_2, 1, 0},
    {0x04, jd_cmd_3, 1, 0},
    {0x05, jd_cmd_4, 1, 0},
    {0x06, jd_cmd_4, 1, 0},
    {0x0B, jd_cmd_5, 1, 0}, // 0x11 = 2 lanes
    {0x17, jd_cmd_4, 1, 0},
    {0x20, jd_cmd_6, 1, 0}, // r_lansel_sel_reg=1
    {0x1F, jd_cmd_7, 1, 0}, // hs_settle time
    {0x23, jd_cmd_4, 1, 0}, // close gas
    {0x25, jd_cmd_8, 1, 0},
    {0x28, jd_cmd_9, 1, 0},
    {0x29, jd_cmd_6, 1, 0}, // revcom
    {0x2A, jd_cmd_2, 1, 0}, // revcom
    {0x2B, jd_cmd_6, 1, 0}, // vcom
    {0x2C, jd_cmd_2, 1, 0}, // vcom
    // Page select 2
    {0x30, jd_cmd_10, 1, 0},
    {0x01, jd_cmd_11, 1, 0},
    {0x03, jd_cmd_12, 1, 0},
    {0x04, jd_cmd_4, 1, 0},
    {0x05, jd_cmd_13, 1, 0},
    {0x0A, jd_cmd_14, 1, 0},
    {0x0B, jd_cmd_15, 11, 0},
    {0x0C, jd_cmd_16, 11, 0},
    {0x0D, jd_cmd_17, 11, 0},
    {0x0E, jd_cmd_16, 11, 0},
    {0x0F, jd_cmd_18, 11, 0},
    {0x10, jd_cmd_16, 11, 0},
    {0x11, jd_cmd_19, 11, 0},
    {0x12, jd_cmd_16, 11, 0},
    {0x14, jd_cmd_20, 4, 0}, // CKV_OFF
    {0x18, jd_cmd_21, 1, 0},
    // Page select 6 - Gamma
    {0x30, jd_cmd_22, 1, 0},
    {0x12, jd_cmd_23, 14, 0},
    {0x13, jd_cmd_23, 14, 0},
    // Page select A
    {0x30, jd_cmd_24, 1, 0},
    {0x02, jd_cmd_25, 1, 0},
    {0x0B, jd_cmd_26, 1, 0},
    {0x12, jd_cmd_27, 1, 0},
    {0x13, jd_cmd_28, 1, 0},
    // Page select D
    {0x30, jd_cmd_29, 1, 0},
    {0x0D, jd_cmd_6, 1, 0},
    {0x10, jd_cmd_30, 1, 0},
    {0x11, jd_cmd_30, 1, 0},
    {0x12, jd_cmd_30, 1, 0},
    {0x13, jd_cmd_30, 1, 0},
    // Page select 0
    {0x30, jd_cmd_0, 1, 0},
    // Sleep Out
    {0x11, jd_cmd_4, 0, 120},
    // Display On
    {0x29, jd_cmd_4, 0, 20},
};

// Use JD9165BA commands for 7-inch panel
#define LCD_INIT_CMDS jd9165ba_lcd_cmds
#define LCD_INIT_CMDS_SIZE                                                     \
  (sizeof(jd9165ba_lcd_cmds) / sizeof(jd9165ba_lcd_cmds[0]))

// ====================================================================================
// GLOBAL HANDLES AND STATE
// ====================================================================================

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static lv_display_t *main_display = NULL;
static sdmmc_card_t *sd_card = NULL;
static bool sd_mounted = false;

// State
static uint8_t current_brightness = 100;
static bool wifi_enabled = false;
static bool wifi_connected = false;
static bool bluetooth_enabled = true;

// Time & SNTP State
static bool time_synced = false;
static time_t now;
static struct tm timeinfo;

// WiFi State
static char wifi_ssid[33] = "";
static char wifi_ip[16] = "0.0.0.0";
static esp_netif_t *sta_netif = NULL;

// WiFi Credentials - loaded from NVS, no hardcoded defaults for security
// Use WiFi page UI to configure credentials
#define WIFI_SSID_DEFAULT ""
#define WIFI_PASS_DEFAULT ""

// Pages
static lv_obj_t *page_home = NULL;
static lv_obj_t *page_settings = NULL;
static lv_obj_t *page_wifi = NULL;
static lv_obj_t *page_bluetooth = NULL;

// WiFi Scan Results
#define WIFI_SCAN_MAX_AP 20
static wifi_ap_record_t wifi_scan_results[WIFI_SCAN_MAX_AP];
static uint16_t wifi_scan_count = 0;
static char wifi_selected_ssid[33] = "";
static char wifi_password_input[65] = "";

static lv_obj_t *label_time = NULL;
static lv_obj_t *label_date = NULL;
static lv_obj_t *icon_wifi = NULL;
static lv_obj_t *icon_bluetooth = NULL;
static lv_obj_t *icon_battery = NULL;
static lv_obj_t *logo_img = NULL;
static lv_obj_t *ui_navbar = NULL;     // Global navbar for z-order control
static lv_obj_t *ui_status_bar = NULL; // Global status bar

// WiFi Page UI Elements
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_keyboard = NULL;
static lv_obj_t *wifi_password_ta = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *wifi_ssid_label = NULL;
static lv_obj_t *wifi_pwd_container = NULL;

// Bluetooth Page UI Elements
static lv_obj_t *bt_list = NULL;
static lv_obj_t *bt_status_label = NULL;
static lv_obj_t *bt_device_label = NULL;

// Gallery Page UI Elements
static lv_obj_t *page_gallery = NULL;
static lv_obj_t *gallery_image = NULL;
static lv_obj_t *gallery_filename_label = NULL;
static lv_obj_t *gallery_index_label = NULL;
static char
    gallery_files[20][256]; // Max 20 files, 256 chars each (d_name max size)
static int gallery_file_count = 0;
static int gallery_current_index = 0;

// ====================================================================================
// COLOR THEME - REPTILE MANAGER
// ====================================================================================

// Main theme - Premium Jungle/Terrarium inspired
// Enhanced color palette for better visual impact

// Backgrounds - Deeper, richer colors
#define COLOR_BG_DARK lv_color_hex(0x0A1510) // Deep jungle black
#define COLOR_BG_CARD                                                          \
  lv_color_hex(0x162B1D) // Dark terrarium green (glassmorphism base)
#define COLOR_BG_CARD_HOVER lv_color_hex(0x1E3A27) // Hover state
#define COLOR_ACCENT lv_color_hex(0x2D5A3D)        // Forest accent (richer)

// Primary colors - More vibrant
#define COLOR_PRIMARY lv_color_hex(0x00E676) // Neon green (eye-catching)
#define COLOR_PRIMARY_DARK                                                     \
  lv_color_hex(0x00C853) // Darker primary for pressed states
#define COLOR_SECONDARY lv_color_hex(0x69F0AE) // Mint green (secondary actions)

// Status colors - Higher contrast
#define COLOR_SUCCESS lv_color_hex(0x00E676) // Bright green (fed, healthy)
#define COLOR_WARNING lv_color_hex(0xFFAB00) // Amber (attention needed)
#define COLOR_DANGER lv_color_hex(0xFF5252)  // Bright red (urgent/overdue)
#define COLOR_INFO lv_color_hex(0x40C4FF)    // Cyan blue (informational)

// Text colors - Better readability
#define COLOR_TEXT lv_color_hex(0xF1F8E9)     // Almost white with green tint
#define COLOR_TEXT_DIM lv_color_hex(0xA5D6A7) // Muted green (secondary text)
#define COLOR_TEXT_MUTED                                                       \
  lv_color_hex(0x6B8E6B) // Even more muted (hints, disabled)

// UI elements
#define COLOR_BORDER lv_color_hex(0x43A047)          // Vibrant green border
#define COLOR_HEADER lv_color_hex(0x1B5E20)          // Dark green header
#define COLOR_HEADER_GRADIENT lv_color_hex(0x2E7D32) // Gradient end for header
#define COLOR_DIVIDER lv_color_hex(0x2E4A3A)         // Subtle divider lines

// Reptile-specific colors - More distinctive
#define COLOR_SNAKE lv_color_hex(0xA1887F)     // Warm brown for snakes
#define COLOR_LIZARD lv_color_hex(0x81C784)    // Fresh green for lizards
#define COLOR_TURTLE lv_color_hex(0x8D6E63)    // Earth brown for turtles
#define COLOR_EGG lv_color_hex(0xFFF8E1)       // Cream white for eggs
#define COLOR_AMPHIBIAN lv_color_hex(0x4DD0E1) // Cyan for amphibians

// Interactive states
#define COLOR_PRESSED lv_color_hex(0x00C853)  // Pressed button state
#define COLOR_DISABLED lv_color_hex(0x37474F) // Disabled elements

// ====================================================================================
// REPTILE MANAGER DATA STRUCTURES
// ====================================================================================

// Animal species types
typedef enum {
  SPECIES_SNAKE = 0,
  SPECIES_LIZARD,
  SPECIES_TURTLE,
  SPECIES_OTHER
} reptile_species_t;

// Animal sex
typedef enum { SEX_UNKNOWN = 0, SEX_MALE, SEX_FEMALE } reptile_sex_t;

// Health status
typedef enum { HEALTH_GOOD = 0, HEALTH_ATTENTION, HEALTH_SICK } health_status_t;
// CITES Annex classification (Règlement UE 338/97)
typedef enum {
  CITES_NOT_LISTED = 0, // Non concerné
  CITES_ANNEX_A,        // Annexe A (CITES I) - Commerce interdit
  CITES_ANNEX_B,        // Annexe B (CITES II) - Commerce régulé
  CITES_ANNEX_C,        // Annexe C (CITES III) - Surveillance
  CITES_ANNEX_D         // Annexe D - Suivi statistique
} cites_annex_t;

// Exit/Sortie reason
typedef enum {
  EXIT_NONE = 0,   // Still in collection
  EXIT_SOLD,       // Vendu
  EXIT_DONATED,    // Donné
  EXIT_DECEASED,   // Décédé
  EXIT_ESCAPED,    // Évasion
  EXIT_CONFISCATED // Confisqué
} exit_reason_t;

// Animal record structure - CONFORME Arrêté 10 août 2004
typedef struct {
  // === Identification unique ===
  uint8_t id;
  char uuid[37]; // UUID v4 format (36 chars + null)

  // === Identification espèce ===
  char name[32];               // Nom usuel
  char species_common[48];     // Nom vernaculaire (ex: "Python Royal")
  char species_scientific[64]; // Nom latin (ex: "Python regius")
  char morph[32];              // Phase/mutation (ex: "Pastel Banana")
  reptile_species_t species;
  reptile_sex_t sex;

  // === Identification individuelle ===
  char microchip[20];   // N° puce ISO 11784/11785 (15 digits)
  char ring_number[16]; // N° bague si applicable

  // === Naissance ===
  uint16_t birth_year;
  uint8_t birth_month;
  uint8_t birth_day;
  bool birth_estimated; // Date estimée vs confirmée

  // === CITES / Réglementation ===
  cites_annex_t cites_annex;
  char cites_permit[32]; // N° permis CITES si annexe A
  char cites_date[16];   // Date du permis (YYYY-MM-DD)
  bool cdc_required;     // Nécessite certificat de capacité

  // === Acquisition/Entrée ===
  time_t date_acquisition;   // Date d'entrée dans l'élevage
  char origin[64];           // Provenance (Élevage X, Animalerie Y, Import)
  char origin_country[3];    // Code pays ISO 3166-1 alpha-2
  char breeder_name[64];     // Nom éleveur/vendeur
  char breeder_address[128]; // Adresse complète
  char breeder_cdc[32];      // N° CDC vendeur si applicable
  bool captive_bred;         // Né en captivité (NC) vs prélevé (W)

  // === Sortie/Cession ===
  time_t date_exit; // Date de sortie (0 si encore présent)
  exit_reason_t exit_reason;
  char recipient_name[64];     // Nom acquéreur/cessionnaire
  char recipient_address[128]; // Adresse acquéreur
  uint16_t sale_price;         // Prix de vente (0 si don)

  // === Données techniques ===
  uint16_t weight_grams; // Poids actuel en grammes
  uint8_t terrarium_id;
  uint16_t purchase_price; // Prix d'achat en euros
  time_t last_feeding;
  time_t last_weight;
  time_t last_shed;
  health_status_t health;
  bool is_breeding;
  char photo_path[64];
  char notes[128];
  bool active; // false = sorti du cheptel
} reptile_t;

// Feeding record
typedef struct {
  uint8_t animal_id;
  time_t timestamp;
  char prey_type[24]; // e.g., "Souris adulte", "Grillon"
  uint8_t prey_count;
  bool accepted; // Did animal eat?
} feeding_record_t;

// Health/Vet record
typedef struct {
  uint8_t animal_id;
  time_t timestamp;
  char event_type[24]; // "Vermifuge", "Mue", "Vétérinaire"
  char description[64];
  uint16_t weight_grams; // Weight at time of event
} health_record_t;

// Breeding/Reproduction record
typedef struct {
  uint8_t id;
  uint8_t female_id;
  uint8_t male_id;
  time_t pairing_date;
  time_t laying_date; // Actual or estimated
  uint8_t egg_count;
  time_t hatch_date; // Actual or estimated (laying + 60 days typical)
  uint8_t hatched_count;
  bool active;
} breeding_record_t;

// Inventory item
typedef struct {
  char name[24];
  uint16_t quantity;
  uint16_t alert_threshold; // Alert when below this
  char unit[8];             // "pcs", "kg", etc.
} inventory_item_t;

// Global reptile data
#define MAX_REPTILES 30
#define MAX_FEEDINGS 100
#define MAX_HEALTH_RECORDS 50
#define MAX_BREEDINGS 10
#define MAX_INVENTORY_ITEMS 10

static reptile_t reptiles[MAX_REPTILES];
static feeding_record_t feedings[MAX_FEEDINGS];
static health_record_t health_records[MAX_HEALTH_RECORDS];
static breeding_record_t breedings[MAX_BREEDINGS];
static inventory_item_t inventory[MAX_INVENTORY_ITEMS];

static uint8_t reptile_count = 0;
static uint8_t feeding_count = 0;
static uint8_t health_record_count = 0;
static uint8_t breeding_count = 0;
static uint8_t inventory_count = 0;

// Reptile Manager UI Elements
static lv_obj_t *page_animals = NULL;
static lv_obj_t *page_animal_detail = NULL;
static lv_obj_t *page_calendar = NULL;
static lv_obj_t *page_breeding = NULL;
static lv_obj_t *page_conformity = NULL;
static lv_obj_t *animal_list = NULL;
static lv_obj_t *detail_name_label = NULL;
static lv_obj_t *detail_info_label = NULL;
static lv_obj_t *dashboard_alerts_label = NULL;
static lv_obj_t *dashboard_snake_count = NULL;
static lv_obj_t *dashboard_lizard_count = NULL;
static lv_obj_t *dashboard_turtle_count = NULL;
static lv_obj_t *conformity_status_label = NULL;
static int selected_animal_id = -1;

// Audio and Battery state
static bool audio_enabled = true;
static uint8_t battery_level = 100; // Simulated battery level

// ====================================================================================
// AUDIO FUNCTIONS (ES8311 Codec via esp_codec_dev - Full implementation)
// ====================================================================================

#if AUDIO_ENABLED
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include <math.h>

static bool audio_initialized = false;
static i2c_master_bus_handle_t audio_i2c_bus = NULL;
static const audio_codec_if_t *es8311_codec_if = NULL;

// Buffer for tone generation
#define AUDIO_BUFFER_SIZE 512
static int16_t audio_buffer[AUDIO_BUFFER_SIZE * 2]; // Stereo

static void audio_generate_tone_stereo(uint32_t freq_hz, int16_t *buffer,
                                       size_t samples) {
  for (size_t i = 0; i < samples; i++) {
    float angle = 2.0f * M_PI * freq_hz * i / AUDIO_SAMPLE_RATE;
    int16_t sample = (int16_t)(sinf(angle) * 16000); // ~50% volume
    buffer[i * 2] = sample;                          // Left
    buffer[i * 2 + 1] = sample;                      // Right
  }
}

static void audio_init(void) {
  if (audio_initialized)
    return;

  ESP_LOGI(TAG, "Initializing ES8311 audio codec...");

  // Configure PA enable pin (NS4150B amplifier)
  gpio_config_t pa_conf = {
      .pin_bit_mask = (1ULL << PA_ENABLE_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&pa_conf);
  gpio_set_level(PA_ENABLE_GPIO, 1); // Enable PA

  // Initialize shared I2C bus for ES8311 and Touch (I2C_NUM_0 on GPIO 7/8)
  // This bus is shared with the touch controller - both use same GPIOs
  if (!i2c_bus_handle) {
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0, // Shared bus with touch
        .scl_io_num = ES8311_I2C_SCL,
        .sda_io_num = ES8311_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create shared I2C bus: %s",
               esp_err_to_name(ret));
      return;
    }
  }

  // Use the shared I2C bus for audio
  audio_i2c_bus = i2c_bus_handle;

  // Create I2C control interface for ES8311
  audio_codec_i2c_cfg_t i2c_cfg = {
      .addr = ES8311_CODEC_DEFAULT_ADDR,
      .bus_handle = audio_i2c_bus,
  };
  const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
  if (!ctrl_if) {
    ESP_LOGE(TAG, "Failed to create I2C control interface");
    return;
  }

  // Create GPIO interface
  const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
  if (!gpio_if) {
    ESP_LOGE(TAG, "Failed to create GPIO interface");
    return;
  }

  // Configure ES8311 codec
  es8311_codec_cfg_t es8311_cfg = {
      .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
      .ctrl_if = ctrl_if,
      .gpio_if = gpio_if,
      .pa_pin = PA_ENABLE_GPIO,
      .use_mclk = false, // Internal clock
      .master_mode = false,
  };
  es8311_codec_if = es8311_codec_new(&es8311_cfg);
  if (!es8311_codec_if) {
    ESP_LOGE(TAG, "Failed to create ES8311 codec interface");
    return;
  }

  // Note: I2S data path disabled due to ESP-IDF 6.1 linker bug
  // The codec is configured and PA is enabled, but no audio output
  // This will be fixed when ESP-IDF patches the linker issue

  audio_initialized = true;
  ESP_LOGI(TAG, "ES8311 codec initialized (I2C @ 0x%02X, PA on GPIO%d)",
           ES8311_CODEC_DEFAULT_ADDR, PA_ENABLE_GPIO);
  ESP_LOGW(
      TAG,
      "Audio playback disabled - ESP-IDF 6.1 linker bug with esp_driver_i2s");
}

static void audio_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
  // Audio playback disabled due to ESP-IDF 6.1 linker bug
  // When esp_driver_i2s is added, it causes:
  // "error: --enable-non-contiguous-regions discards section `.text.delay_us'"
  (void)freq_hz;
  (void)duration_ms;
}

#else
static void audio_init(void) {
  ESP_LOGI(TAG, "Audio disabled (AUDIO_ENABLED=0)");
}
static void audio_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
  (void)freq_hz;
  (void)duration_ms;
}
#endif

// UI Sound effects
static void sound_click(void) { audio_play_tone(SOUND_CLICK_FREQ, 30); }

static void sound_success(void) {
  audio_play_tone(SOUND_SUCCESS_FREQ, 100);
  vTaskDelay(pdMS_TO_TICKS(50));
  audio_play_tone(SOUND_SUCCESS_FREQ + 500, 100);
}

static void sound_error(void) { audio_play_tone(SOUND_ERROR_FREQ, 200); }

static void sound_alert(void) {
  for (int i = 0; i < 3; i++) {
    audio_play_tone(SOUND_ALERT_FREQ, 100);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ====================================================================================
// BATTERY FUNCTIONS
// ====================================================================================

static uint8_t battery_get_level(void) {
#if BATTERY_SIMULATED
  // Simulated battery - slowly decreases then resets
  static uint32_t last_update = 0;
  uint32_t now = xTaskGetTickCount();
  if (now - last_update > pdMS_TO_TICKS(60000)) { // Every minute
    last_update = now;
    if (battery_level > 10) {
      battery_level -= 1;
    } else {
      battery_level = 100; // Reset for demo
    }
  }
  return battery_level;
#elif BATTERY_ENABLED
  // TODO: Read from fuel gauge (MAX17048, BQ27441, etc.)
  return 100;
#else
  return 100; // Always full if no battery
#endif
}

static const char *battery_get_icon(uint8_t level) {
  if (level > 75)
    return LV_SYMBOL_BATTERY_FULL;
  if (level > 50)
    return LV_SYMBOL_BATTERY_3;
  if (level > 25)
    return LV_SYMBOL_BATTERY_2;
  if (level > 10)
    return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

// ====================================================================================
// EXPORT & CONFORMITÉ FUNCTIONS (Arrêté 10 août 2004)
// ====================================================================================

// Helper: Get CITES annex string
static const char *cites_annex_to_string(cites_annex_t annex) {
  switch (annex) {
  case CITES_ANNEX_A:
    return "A";
  case CITES_ANNEX_B:
    return "B";
  case CITES_ANNEX_C:
    return "C";
  case CITES_ANNEX_D:
    return "D";
  default:
    return "Non listé";
  }
}

// Helper: Get exit reason string
static const char *exit_reason_to_string(exit_reason_t reason) {
  switch (reason) {
  case EXIT_SOLD:
    return "Vente";
  case EXIT_DONATED:
    return "Don";
  case EXIT_DECEASED:
    return "Décès";
  case EXIT_ESCAPED:
    return "Évasion";
  case EXIT_CONFISCATED:
    return "Confiscation";
  default:
    return "";
  }
}

// Helper: Format timestamp to date string
static void format_date(time_t timestamp, char *buf, size_t len) {
  if (timestamp == 0) {
    buf[0] = '\0';
    return;
  }
  struct tm *tm_info = localtime(&timestamp);
  strftime(buf, len, "%Y-%m-%d", tm_info);
}

/**
 * @brief Export le registre des animaux au format CSV
 * @param filepath Chemin du fichier de sortie (ex: "/sdcard/registre.csv")
 * @return ESP_OK si succès, code erreur sinon
 *
 * Format conforme à l'arrêté du 10 août 2004 avec les champs:
 * ID, Nom, Espèce_Commune, Espèce_Scientifique, Identification, Sexe,
 * Date_Naissance, CITES, Date_Entrée, Provenance, Date_Sortie, Destination
 */
static esp_err_t export_registre_csv(const char *filepath) {
  if (!sd_mounted) {
    ESP_LOGE(TAG, "SD Card not mounted, cannot export");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Exporting registre to CSV: %s", filepath);

  FILE *f = fopen(filepath, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to create CSV file: %s", filepath);
    return ESP_ERR_NOT_FOUND;
  }

  // CSV Header (conforme arrêté 10 août 2004)
  fprintf(f,
          "ID,UUID,Nom,Espece_Commune,Espece_Scientifique,Identification,Sexe,"
          "Date_Naissance,Naissance_Estimee,CITES_Annexe,CITES_Permis,"
          "Date_Entree,Provenance,Pays_Origine,Eleveur_Nom,Ne_Captivite,"
          "Date_Sortie,Motif_Sortie,Destinataire_Nom,Destinataire_Adresse,"
          "Poids_Grammes,Actif\n");

  // Export each animal
  char date_birth[16], date_acq[16], date_exit[16];

  for (int i = 0; i < reptile_count; i++) {
    reptile_t *r = &reptiles[i];

    // Format dates
    if (r->birth_year > 0) {
      snprintf(date_birth, sizeof(date_birth), "%04d-%02d-%02d", r->birth_year,
               r->birth_month ? r->birth_month : 1,
               r->birth_day ? r->birth_day : 1);
    } else {
      date_birth[0] = '\0';
    }
    format_date(r->date_acquisition, date_acq, sizeof(date_acq));
    format_date(r->date_exit, date_exit, sizeof(date_exit));

    // Write CSV line (escape quotes in strings)
    fprintf(f,
            "%d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%s,"
            "%s,%s,%s,\"%s\","
            "%s,\"%s\",\"%s\",\"%s\",%s,"
            "%s,%s,\"%s\",\"%s\","
            "%d,%s\n",
            r->id, r->uuid, r->name, r->species_common, r->species_scientific,
            r->microchip,
            (r->sex == SEX_MALE)     ? "M"
            : (r->sex == SEX_FEMALE) ? "F"
                                     : "?",
            date_birth, r->birth_estimated ? "Oui" : "Non",
            cites_annex_to_string(r->cites_annex), r->cites_permit, date_acq,
            r->origin, r->origin_country, r->breeder_name,
            r->captive_bred ? "Oui" : "Non", date_exit,
            exit_reason_to_string(r->exit_reason), r->recipient_name,
            r->recipient_address, r->weight_grams, r->active ? "Oui" : "Non");
  }

  fclose(f);

  ESP_LOGI(TAG, "Registre exported: %d animals to %s", reptile_count, filepath);
  return ESP_OK;
}

/**
 * @brief Génère une attestation de cession d'animal non domestique
 * @param animal_id ID de l'animal à céder
 * @param recipient_name Nom du cessionnaire
 * @param recipient_address Adresse du cessionnaire
 * @param sale_price Prix de vente (0 si don)
 * @param filepath Chemin du fichier de sortie
 * @return ESP_OK si succès
 *
 * Format conforme aux exigences réglementaires françaises
 */
static esp_err_t create_attestation_cession(uint8_t animal_id,
                                            const char *recipient_name,
                                            const char *recipient_address,
                                            uint16_t sale_price,
                                            const char *filepath) {
  if (!sd_mounted) {
    ESP_LOGE(TAG, "SD Card not mounted, cannot create attestation");
    return ESP_ERR_INVALID_STATE;
  }

  // Find animal
  reptile_t *animal = NULL;
  for (int i = 0; i < reptile_count; i++) {
    if (reptiles[i].id == animal_id) {
      animal = &reptiles[i];
      break;
    }
  }

  if (!animal) {
    ESP_LOGE(TAG, "Animal ID %d not found", animal_id);
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG, "Creating attestation for animal: %s", animal->name);

  FILE *f = fopen(filepath, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to create attestation file: %s", filepath);
    return ESP_ERR_NOT_FOUND;
  }

  // Get current date
  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  char date_now[32];
  strftime(date_now, sizeof(date_now), "%d/%m/%Y", tm_now);

  // Format birth date
  char birth_date[32];
  if (animal->birth_year > 0) {
    snprintf(birth_date, sizeof(birth_date), "%02d/%02d/%04d%s",
             animal->birth_day ? animal->birth_day : 1,
             animal->birth_month ? animal->birth_month : 1, animal->birth_year,
             animal->birth_estimated ? " (estimé)" : "");
  } else {
    snprintf(birth_date, sizeof(birth_date), "Inconnue");
  }

  // Write attestation
  fprintf(f, "================================================================="
             "==============\n");
  fprintf(f, "               ATTESTATION DE CESSION D'ANIMAL NON DOMESTIQUE\n");
  fprintf(f, "        (Article L.413-6 du Code de l'environnement - Arrêté du "
             "10/08/2004)\n");
  fprintf(f, "================================================================="
             "==============\n\n");

  fprintf(f, "CÉDANT:\n");
  fprintf(f, "-----------------------------------------------------------------"
             "-------\n");
  fprintf(f, "Nom / Raison sociale : [À COMPLÉTER]\n");
  fprintf(f, "Adresse              : [À COMPLÉTER]\n");
  fprintf(f, "Téléphone            : [À COMPLÉTER]\n");
  fprintf(f, "Email                : [À COMPLÉTER]\n");
  fprintf(f, "N° CDC (si applicable): [À COMPLÉTER]\n\n");

  fprintf(f, "CESSIONNAIRE (Acquéreur):\n");
  fprintf(f, "-----------------------------------------------------------------"
             "-------\n");
  fprintf(f, "Nom / Raison sociale : %s\n",
          recipient_name[0] ? recipient_name : "[À COMPLÉTER]");
  fprintf(f, "Adresse              : %s\n",
          recipient_address[0] ? recipient_address : "[À COMPLÉTER]");
  fprintf(f, "Téléphone            : [À COMPLÉTER]\n");
  fprintf(f, "Email                : [À COMPLÉTER]\n\n");

  fprintf(f, "ANIMAL CÉDÉ:\n");
  fprintf(f, "-----------------------------------------------------------------"
             "-------\n");
  fprintf(f, "Nom usuel            : %s\n", animal->name);
  fprintf(f, "Espèce (vernaculaire): %s\n", animal->species_common);
  fprintf(f, "Espèce (scientifique): %s\n",
          animal->species_scientific[0] ? animal->species_scientific
                                        : "[À COMPLÉTER]");
  fprintf(f, "Sexe                 : %s\n",
          animal->sex == SEX_MALE     ? "Mâle"
          : animal->sex == SEX_FEMALE ? "Femelle"
                                      : "Indéterminé");
  fprintf(f, "Date de naissance    : %s\n", birth_date);
  fprintf(f, "N° Identification    : %s\n",
          animal->microchip[0] ? animal->microchip : "Non pucé");
  fprintf(f, "Phase/Mutation       : %s\n",
          animal->morph[0] ? animal->morph : "-");
  fprintf(f, "Poids actuel         : %d g\n", animal->weight_grams);
  fprintf(f, "Origine              : %s\n",
          animal->captive_bred ? "Né en captivité (NC)" : "Prélevé (W)");
  fprintf(f, "\n");

  fprintf(f, "STATUT RÉGLEMENTAIRE:\n");
  fprintf(f, "-----------------------------------------------------------------"
             "-------\n");
  fprintf(f, "Annexe CITES/UE      : %s\n",
          cites_annex_to_string(animal->cites_annex));
  if (animal->cites_annex == CITES_ANNEX_A && animal->cites_permit[0]) {
    fprintf(f, "N° Permis CITES      : %s\n", animal->cites_permit);
    fprintf(f, "Date du permis       : %s\n", animal->cites_date);
  }
  fprintf(f, "CDC requis           : %s\n",
          animal->cdc_required ? "Oui" : "Non");
  fprintf(f, "\n");

  fprintf(f, "CONDITIONS DE LA CESSION:\n");
  fprintf(f, "-----------------------------------------------------------------"
             "-------\n");
  if (sale_price > 0) {
    fprintf(f, "Type                 : Vente\n");
    fprintf(f, "Prix                 : %d €\n", sale_price);
  } else {
    fprintf(f, "Type                 : Don (à titre gratuit)\n");
  }
  fprintf(f, "Date de cession      : %s\n", date_now);
  fprintf(f, "\n");

  fprintf(f, "DÉCLARATIONS DU CESSIONNAIRE:\n");
  fprintf(f, "-----------------------------------------------------------------"
             "-------\n");
  fprintf(f, "Le cessionnaire déclare:\n");
  fprintf(f,
          "[ ] Avoir pris connaissance des besoins spécifiques de l'espèce\n");
  fprintf(
      f,
      "[ ] Disposer d'installations adaptées à l'hébergement de cet animal\n");
  fprintf(f, "[ ] Connaître la réglementation applicable à la détention de "
             "cette espèce\n");
  fprintf(f, "[ ] S'engager à assurer le bien-être de l'animal\n");
  if (animal->cdc_required) {
    fprintf(f, "[ ] Être titulaire du Certificat de Capacité requis\n");
    fprintf(f, "    N° CDC: ________________________\n");
  }
  fprintf(f, "\n");

  fprintf(f, "SIGNATURES:\n");
  fprintf(f, "-----------------------------------------------------------------"
             "-------\n");
  fprintf(f, "\n");
  fprintf(f, "Fait à _________________________, le %s\n", date_now);
  fprintf(f, "\n");
  fprintf(f, "Signature du CÉDANT:              Signature du CESSIONNAIRE:\n");
  fprintf(f, "(précédée de la mention           (précédée de la mention\n");
  fprintf(f, " \"Lu et approuvé\")                \"Lu et approuvé\")\n");
  fprintf(f, "\n\n");
  fprintf(f, "\n\n");
  fprintf(f, "================================================================="
             "==============\n");
  fprintf(f, "Ce document doit être conservé par les deux parties pendant 5 "
             "ans minimum.\n");
  fprintf(f, "Généré par Reptile Panel - ID Animal: %s\n", animal->uuid);
  fprintf(f, "================================================================="
             "==============\n");

  fclose(f);

  // Update animal record with exit info
  animal->date_exit = now;
  animal->exit_reason = (sale_price > 0) ? EXIT_SOLD : EXIT_DONATED;
  strncpy(animal->recipient_name, recipient_name,
          sizeof(animal->recipient_name) - 1);
  strncpy(animal->recipient_address, recipient_address,
          sizeof(animal->recipient_address) - 1);
  animal->sale_price = sale_price;
  animal->active = false;

  ESP_LOGI(TAG, "Attestation created: %s", filepath);
  return ESP_OK;
}

// ====================================================================================
// SD CARD FUNCTIONS
// ====================================================================================

static esp_err_t sd_card_init(void) {
  ESP_LOGI(TAG, "Initializing SD card in SDMMC mode...");
  ESP_LOGI(TAG, "  SD pins: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d",
           SD_CLK_GPIO, SD_CMD_GPIO, SD_D0_GPIO, SD_D1_GPIO, SD_D2_GPIO,
           SD_D3_GPIO);

  // Use SDMMC Slot 0 for SD card (Slot 1 is used by esp_hosted for C6)
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;          // Explicitly use Slot 0
  host.max_freq_khz = SDMMC_FREQ_DEFAULT; // Lower freq for stability

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4; // 4-bit mode
  slot_config.clk = SD_CLK_GPIO;
  slot_config.cmd = SD_CMD_GPIO;
  slot_config.d0 = SD_D0_GPIO;
  slot_config.d1 = SD_D1_GPIO;
  slot_config.d2 = SD_D2_GPIO;
  slot_config.d3 = SD_D3_GPIO;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024};

  ESP_LOGI(TAG, "  Attempting to mount SD card...");

  esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config,
                                          &mount_config, &sd_card);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount SD card filesystem (check format: FAT32)");
    } else if (ret == ESP_ERR_TIMEOUT) {
      ESP_LOGE(TAG, "SD card timeout - check card insertion!");
    } else if (ret == ESP_ERR_INVALID_RESPONSE) {
      ESP_LOGE(TAG, "SD card invalid response - check wiring or card");
    } else {
      ESP_LOGE(TAG, "Failed to mount SD card: %s (0x%x)", esp_err_to_name(ret),
               ret);
    }
    sd_mounted = false;
    return ret;
  }

  sdmmc_card_print_info(stdout, sd_card);
  sd_mounted = true;
  ESP_LOGI(TAG, "SD card mounted successfully at %s", SD_MOUNT_POINT);

  // List files in /sdcard/imgs
  DIR *dir = opendir(SD_MOUNT_POINT "/imgs");
  if (dir) {
    ESP_LOGI(TAG, "Files in %s/imgs:", SD_MOUNT_POINT);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      ESP_LOGI(TAG, "  - %s", entry->d_name);
    }
    closedir(dir);
  } else {
    ESP_LOGI(TAG, "Directory %s/imgs not found (create it for images)",
             SD_MOUNT_POINT);
  }

  return ESP_OK;
}

// ====================================================================================
// WIFI FUNCTIONS (via ESP32-C6 co-processor)
// ====================================================================================

// Forward declaration for NVS functions (defined later)
static esp_err_t wifi_save_credentials(const char *ssid, const char *password);

// Forward declaration for SNTP functions (defined later)
static void app_sntp_init(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
      ESP_LOGI(WIFI_TAG, "WiFi STA started, connecting...");
      esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_CONNECTED:
      ESP_LOGI(WIFI_TAG, "Connected to AP!");
      wifi_event_sta_connected_t *conn_event =
          (wifi_event_sta_connected_t *)event_data;
      snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", conn_event->ssid);
      break;
    case WIFI_EVENT_STA_DISCONNECTED: {
      wifi_event_sta_disconnected_t *disc_event =
          (wifi_event_sta_disconnected_t *)event_data;
      ESP_LOGW(WIFI_TAG, "Disconnected from AP! Reason: %d",
               disc_event->reason);

      // Log common reasons for debugging
      switch (disc_event->reason) {
      case 2:
        ESP_LOGW(WIFI_TAG, "  -> AUTH_EXPIRE");
        break;
      case 15:
        ESP_LOGW(WIFI_TAG, "  -> 4WAY_HANDSHAKE_TIMEOUT (wrong password?)");
        break;
      case 201:
        ESP_LOGW(WIFI_TAG, "  -> NO_AP_FOUND");
        break;
      case 202:
        ESP_LOGW(WIFI_TAG, "  -> AUTH_FAIL (wrong password)");
        break;
      case 203:
        ESP_LOGW(WIFI_TAG, "  -> ASSOC_FAIL");
        break;
      default:
        ESP_LOGW(WIFI_TAG, "  -> Unknown reason");
        break;
      }

      wifi_connected = false;
      memset(wifi_ssid, 0, sizeof(wifi_ssid));
      memset(wifi_ip, 0, sizeof(wifi_ip));

      // Update UI to show failure (if we have lock)
      if (lvgl_port_lock(10)) {
        if (wifi_status_label) {
          char status_msg[64];
          snprintf(status_msg, sizeof(status_msg),
                   "Connection failed (reason: %d)", disc_event->reason);
          lv_label_set_text(wifi_status_label, status_msg);
        }
        lvgl_port_unlock();
      }

      // Only retry a few times for password errors
      static int retry_count = 0;
      if (disc_event->reason == 15 || disc_event->reason == 202) {
        // Wrong password - don't retry infinitely
        if (retry_count < 2) {
          retry_count++;
          ESP_LOGI(WIFI_TAG, "Retrying connection (attempt %d)...",
                   retry_count);
          esp_wifi_connect();
        } else {
          ESP_LOGE(WIFI_TAG, "Authentication failed - check password!");
          retry_count = 0;
        }
      } else if (wifi_enabled && retry_count < 5) {
        retry_count++;
        ESP_LOGI(WIFI_TAG, "Retrying connection (attempt %d)...", retry_count);
        esp_wifi_connect();
      } else {
        retry_count = 0;
      }
      break;
    }
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(WIFI_TAG, "Connected! Got IP: %s", wifi_ip);
    wifi_connected = true;

    // Save successful credentials to NVS for auto-reconnect
    if (strlen(wifi_selected_ssid) > 0 && strlen(wifi_password_input) > 0) {
      wifi_save_credentials(wifi_selected_ssid, wifi_password_input);
    }

    // Start SNTP to sync time
    if (!esp_sntp_enabled()) {
      app_sntp_init();
    }

    // Update UI to show success
    if (lvgl_port_lock(10)) {
      if (wifi_status_label) {
        char status_msg[64];
        snprintf(status_msg, sizeof(status_msg), "Connecté! IP: %s", wifi_ip);
        lv_label_set_text(wifi_status_label, status_msg);
      }
      if (icon_wifi) {
        lv_obj_set_style_text_color(icon_wifi, COLOR_SUCCESS, 0);
      }
      lvgl_port_unlock();
    }
  }
}

// ====================================================================================
// WIFI NVS STORAGE - Save/Load favorite networks
// ====================================================================================

#define NVS_WIFI_NAMESPACE "wifi_creds"
#define NVS_WIFI_SSID_KEY "saved_ssid"
#define NVS_WIFI_PASS_KEY "saved_pass"

// Save WiFi credentials to NVS
static esp_err_t wifi_save_credentials(const char *ssid, const char *password) {
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(WIFI_TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_set_str(nvs_handle, NVS_WIFI_SSID_KEY, ssid);
  if (ret != ESP_OK) {
    ESP_LOGE(WIFI_TAG, "Failed to save SSID: %s", esp_err_to_name(ret));
    nvs_close(nvs_handle);
    return ret;
  }

  ret = nvs_set_str(nvs_handle, NVS_WIFI_PASS_KEY, password);
  if (ret != ESP_OK) {
    ESP_LOGE(WIFI_TAG, "Failed to save password: %s", esp_err_to_name(ret));
    nvs_close(nvs_handle);
    return ret;
  }

  ret = nvs_commit(nvs_handle);
  nvs_close(nvs_handle);

  if (ret == ESP_OK) {
    ESP_LOGI(WIFI_TAG, "WiFi credentials saved for SSID: %s", ssid);
  }
  return ret;
}

// Load WiFi credentials from NVS
static esp_err_t wifi_load_credentials(char *ssid, size_t ssid_len,
                                       char *password, size_t pass_len) {
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(WIFI_TAG, "No saved WiFi credentials found");
    return ret;
  }

  // Use local copies since nvs_get_str modifies size by reference
  size_t local_ssid_len = ssid_len;
  size_t local_pass_len = pass_len;

  ret = nvs_get_str(nvs_handle, NVS_WIFI_SSID_KEY, ssid, &local_ssid_len);
  if (ret != ESP_OK) {
    nvs_close(nvs_handle);
    return ret;
  }

  ret = nvs_get_str(nvs_handle, NVS_WIFI_PASS_KEY, password, &local_pass_len);
  nvs_close(nvs_handle);

  if (ret == ESP_OK) {
    ESP_LOGI(WIFI_TAG, "Loaded saved WiFi credentials for SSID: %s", ssid);
  }
  return ret;
}

// Delete saved WiFi credentials
static esp_err_t wifi_delete_credentials(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  nvs_erase_key(nvs_handle, NVS_WIFI_SSID_KEY);
  nvs_erase_key(nvs_handle, NVS_WIFI_PASS_KEY);
  ret = nvs_commit(nvs_handle);
  nvs_close(nvs_handle);

  ESP_LOGI(WIFI_TAG, "Saved WiFi credentials deleted");
  return ret;
}

// Check if WiFi credentials are saved
static bool wifi_has_saved_credentials(void) {
  nvs_handle_t nvs_handle;
  if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &nvs_handle) != ESP_OK) {
    return false;
  }

  size_t required_size = 0;
  esp_err_t ret =
      nvs_get_str(nvs_handle, NVS_WIFI_SSID_KEY, NULL, &required_size);
  nvs_close(nvs_handle);

  return (ret == ESP_OK && required_size > 1);
}

static esp_err_t wifi_init(void) {
  ESP_LOGI(WIFI_TAG, "Initializing WiFi via ESP32-C6...");

  // Initialize TCP/IP stack
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create default WiFi station
  sta_netif = esp_netif_create_default_wifi_sta();

  // Initialize WiFi with default config
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL));

  // Configure WiFi
  wifi_config_t wifi_cfg = {
      .sta =
          {
              .ssid = WIFI_SSID_DEFAULT,
              .password = WIFI_PASS_DEFAULT,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

  ESP_LOGI(WIFI_TAG, "WiFi initialized, ready to connect");
  return ESP_OK;
}

static esp_err_t wifi_start(void) {
  if (!wifi_enabled) {
    ESP_LOGI(WIFI_TAG, "Starting WiFi...");
    esp_err_t ret = esp_wifi_start();
    if (ret == ESP_OK) {
      wifi_enabled = true;
    }
    return ret;
  }
  return ESP_OK;
}

static esp_err_t wifi_stop(void) {
  if (wifi_enabled) {
    ESP_LOGI(WIFI_TAG, "Stopping WiFi...");
    esp_wifi_disconnect();
    esp_wifi_stop();
    wifi_enabled = false;
    wifi_connected = false;
  }
  return ESP_OK;
}

// RSSI comparison function for qsort (descending - strongest signal first)
static int rssi_compare(const void *a, const void *b) {
  const wifi_ap_record_t *ap_a = (const wifi_ap_record_t *)a;
  const wifi_ap_record_t *ap_b = (const wifi_ap_record_t *)b;
  // Higher RSSI (less negative) should come first
  return ap_b->rssi - ap_a->rssi;
}

// WiFi Scan function
static esp_err_t wifi_scan(void) {
  ESP_LOGI(WIFI_TAG, "Starting WiFi scan...");

  // Ensure WiFi is started
  if (!wifi_enabled) {
    ESP_LOGI(WIFI_TAG, "WiFi not enabled, starting...");
    esp_wifi_start();
    wifi_enabled = true;
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for WiFi to fully start
  }

  // Disconnect first to stop connection retry loop - this is required for scan
  // to work
  ESP_LOGI(WIFI_TAG, "Disconnecting to allow scan...");
  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(500)); // Wait for disconnect to complete

  // Stop any ongoing scan first
  esp_wifi_scan_stop();
  vTaskDelay(pdMS_TO_TICKS(100));

  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = true, // Show all networks
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
      .scan_time.active.min = 120,
      .scan_time.active.max = 300,
  };

  ESP_LOGI(WIFI_TAG, "Starting scan...");
  esp_err_t ret = esp_wifi_scan_start(&scan_config, true); // Blocking scan
  if (ret != ESP_OK) {
    ESP_LOGE(WIFI_TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
    return ret;
  }

  uint16_t ap_count = 0;
  ret = esp_wifi_scan_get_ap_num(&ap_count);
  if (ret != ESP_OK) {
    ESP_LOGE(WIFI_TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(WIFI_TAG, "Scan found %d APs", ap_count);

  if (ap_count == 0) {
    wifi_scan_count = 0;
    return ESP_OK;
  }

  // Limit to max we can store
  if (ap_count > WIFI_SCAN_MAX_AP) {
    ap_count = WIFI_SCAN_MAX_AP;
  }

  wifi_ap_record_t temp_results[WIFI_SCAN_MAX_AP];
  ret = esp_wifi_scan_get_ap_records(&ap_count, temp_results);
  if (ret != ESP_OK) {
    ESP_LOGE(WIFI_TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
    return ret;
  }

  // Filter out empty SSIDs and copy valid ones
  wifi_scan_count = 0;
  for (int i = 0; i < ap_count && wifi_scan_count < WIFI_SCAN_MAX_AP; i++) {
    // Skip networks with empty SSID
    if (temp_results[i].ssid[0] != '\0') {
      memcpy(&wifi_scan_results[wifi_scan_count], &temp_results[i],
             sizeof(wifi_ap_record_t));
      wifi_scan_count++;
    }
  }

  // Sort by RSSI (strongest signal first)
  if (wifi_scan_count > 1) {
    qsort(wifi_scan_results, wifi_scan_count, sizeof(wifi_ap_record_t),
          rssi_compare);
  }

  ESP_LOGI(WIFI_TAG, "Found %d valid networks (sorted by signal strength)",
           wifi_scan_count);
  for (int i = 0; i < wifi_scan_count; i++) {
    ESP_LOGI(WIFI_TAG, "  %d: %s (RSSI: %d dBm)", i + 1,
             wifi_scan_results[i].ssid, wifi_scan_results[i].rssi);
  }

  return ESP_OK;
}

// Connect to specific WiFi
static esp_err_t wifi_connect_to(const char *ssid, const char *password) {
  ESP_LOGI(WIFI_TAG, "Connecting to: %s", ssid);

  wifi_config_t wifi_cfg = {0};
  // Use memcpy with length check to avoid strncpy truncation warnings
  size_t ssid_len = strlen(ssid);
  if (ssid_len > sizeof(wifi_cfg.sta.ssid) - 1) {
    ssid_len = sizeof(wifi_cfg.sta.ssid) - 1;
  }
  memcpy(wifi_cfg.sta.ssid, ssid, ssid_len);

  size_t pass_len = strlen(password);
  if (pass_len > sizeof(wifi_cfg.sta.password) - 1) {
    pass_len = sizeof(wifi_cfg.sta.password) - 1;
  }
  memcpy(wifi_cfg.sta.password, password, pass_len);
  wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  esp_wifi_disconnect();
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

  esp_err_t ret = esp_wifi_connect();
  if (ret == ESP_OK) {
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
  }
  return ret;
}

// ====================================================================================
// TIME & SNTP FUNCTIONS
// ====================================================================================

static void time_sync_notification_cb(struct timeval *tv) {
  ESP_LOGI(TAG, "SNTP time synchronized!");
  time_synced = true;
}

static void app_sntp_init(void) {
  ESP_LOGI(TAG, "Initializing SNTP...");

  // Set timezone to Paris (CET-1CEST,M3.5.0,M10.5.0/3)
  // CET = UTC+1, CEST = UTC+2 (daylight saving)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_setservername(2, "time.cloudflare.com");
  esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();

  ESP_LOGI(TAG, "SNTP initialized, waiting for time sync...");
}

static void app_sntp_stop(void) {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
    time_synced = false;
    ESP_LOGI(TAG, "SNTP stopped");
  }
}

static bool get_current_time(struct tm *timeinfo_out) {
  time(&now);
  localtime_r(&now, &timeinfo);

  // Check if time is valid (year > 2020)
  if (timeinfo.tm_year < (2020 - 1900)) {
    return false;
  }

  if (timeinfo_out) {
    *timeinfo_out = timeinfo;
  }
  return true;
}

// ====================================================================================
// BLUETOOTH FUNCTIONS (via ESP32-C6 co-processor)
// ====================================================================================

#if CONFIG_BT_ENABLED

static const char *BT_TAG = "BLUETOOTH";

// Bluetooth state
static bool bt_initialized = false;
static bool bt_scanning = false;
static bool bt_connecting = false;
static bool bt_scan_update_pending = false; // Flag to update UI from main loop
static int bt_selected_device_idx = -1;

// BLE scan results storage
#define BT_SCAN_MAX_DEVICES 10
#define BLE_DEVICE_NAME_MAX_LEN 32 // Max BLE device name length
typedef struct {
  esp_bd_addr_t bda;
  char name[BLE_DEVICE_NAME_MAX_LEN + 1];
  int rssi;
  bool valid;
} bt_device_info_t;

static bt_device_info_t bt_scan_results[BT_SCAN_MAX_DEVICES];
static int bt_scan_count = 0;

// Helper function to convert BDA to string
static char *bda_to_str(esp_bd_addr_t bda, char *str, size_t size) {
  if (bda == NULL || str == NULL || size < 18) {
    return NULL;
  }
  sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3],
          bda[4], bda[5]);
  return str;
}

// BLE GAP event callback
static void bt_gap_ble_cb(esp_gap_ble_cb_event_t event,
                          esp_ble_gap_cb_param_t *param) {
  char bda_str[18];

  switch (event) {
  case ESP_GAP_BLE_SCAN_RESULT_EVT:
    if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
      // Found a BLE device
      ESP_LOGI(BT_TAG, "BLE Device found: %s, RSSI: %d",
               bda_to_str(param->scan_rst.bda, bda_str, sizeof(bda_str)),
               param->scan_rst.rssi);

      // Store device info if we have space - check for duplicates first
      int existing_idx = -1;
      for (int i = 0; i < bt_scan_count; i++) {
        if (memcmp(bt_scan_results[i].bda, param->scan_rst.bda,
                   sizeof(esp_bd_addr_t)) == 0) {
          existing_idx = i;
          break;
        }
      }

      // Use existing slot or new slot if not a duplicate
      int slot_idx = (existing_idx >= 0) ? existing_idx : bt_scan_count;

      if (slot_idx < BT_SCAN_MAX_DEVICES) {
        memcpy(bt_scan_results[slot_idx].bda, param->scan_rst.bda,
               sizeof(esp_bd_addr_t));
        bt_scan_results[slot_idx].rssi = param->scan_rst.rssi;

        // Try to get device name from advertising data
        uint8_t *adv_name = NULL;
        uint8_t adv_name_len = 0;
        adv_name = esp_ble_resolve_adv_data(
            param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
        if (adv_name == NULL) {
          adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                              ESP_BLE_AD_TYPE_NAME_SHORT,
                                              &adv_name_len);
        }

        if (adv_name && adv_name_len > 0) {
          int copy_len = (adv_name_len > BLE_DEVICE_NAME_MAX_LEN)
                             ? BLE_DEVICE_NAME_MAX_LEN
                             : adv_name_len;
          memcpy(bt_scan_results[slot_idx].name, adv_name, copy_len);
          bt_scan_results[slot_idx].name[copy_len] = '\0';
          if (existing_idx < 0) {
            ESP_LOGI(BT_TAG, "  Name: %s", bt_scan_results[slot_idx].name);
          }
        } else if (existing_idx < 0 ||
                   strcmp(bt_scan_results[slot_idx].name, "(Unknown)") == 0) {
          // Only set to Unknown if it's new or was already Unknown
          snprintf(bt_scan_results[slot_idx].name,
                   sizeof(bt_scan_results[slot_idx].name), "(Unknown)");
        }

        bt_scan_results[slot_idx].valid = true;

        // Only increment count if this is a new device
        if (existing_idx < 0) {
          bt_scan_count++;
        }
      }
    } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
      ESP_LOGI(BT_TAG, "BLE Scan complete, found %d devices", bt_scan_count);
      bt_scanning = false;
      bt_scan_update_pending = true; // Signal UI update needed
    }
    break;

  case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
    if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      ESP_LOGI(BT_TAG, "BLE scan started successfully");
      bt_scanning = true;
    } else {
      ESP_LOGE(BT_TAG, "BLE scan start failed: %d",
               param->scan_start_cmpl.status);
      bt_scanning = false;
    }
    break;

  case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
    ESP_LOGI(BT_TAG, "BLE scan stopped");
    bt_scanning = false;
    break;

  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    ESP_LOGI(BT_TAG, "Advertising data set complete");
    break;

  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      ESP_LOGI(BT_TAG,
               "Advertising started - Device visible as 'Reptile Panel'");
    } else {
      ESP_LOGW(BT_TAG, "Advertising start failed: %d",
               param->adv_start_cmpl.status);
    }
    break;

  case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
    ESP_LOGI(BT_TAG, "Advertising stopped");
    break;

  case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
    ESP_LOGI(
        BT_TAG,
        "Connection params updated: status=%d, conn_int=%d, latency=%d, "
        "timeout=%d",
        param->update_conn_params.status, param->update_conn_params.conn_int,
        param->update_conn_params.latency, param->update_conn_params.timeout);
    break;

  default:
    ESP_LOGD(BT_TAG, "BLE GAP event: %d", event);
    break;
  }
}

// Initialize Bluetooth via ESP-Hosted
static esp_err_t bluetooth_init(void) {
  ESP_LOGI(BT_TAG, "Initializing Bluetooth via ESP32-C6...");

  // Ensure ESP-Hosted connection is established
  // This should already be done by WiFi, but let's make sure
  esp_err_t ret = esp_hosted_connect_to_slave();
  if (ret != ESP_OK) {
    ESP_LOGW(BT_TAG,
             "esp_hosted_connect_to_slave: %s (may already be connected)",
             esp_err_to_name(ret));
    // Continue anyway, might already be connected
  }

  // Initialize Bluetooth controller on ESP32-C6
  ret = esp_hosted_bt_controller_init();
  if (ret != ESP_OK) {
    ESP_LOGW(BT_TAG, "BT controller init: %s (may already be initialized)",
             esp_err_to_name(ret));
    // Continue anyway, might already be initialized
  }

  // Enable Bluetooth controller
  ret = esp_hosted_bt_controller_enable();
  if (ret != ESP_OK) {
    ESP_LOGW(BT_TAG, "BT controller enable: %s (may already be enabled)",
             esp_err_to_name(ret));
    // Continue anyway, might already be enabled
  }

  // Open HCI transport for Bluedroid
  hosted_hci_bluedroid_open();

  // Get and attach HCI driver operations
  esp_bluedroid_hci_driver_operations_t hci_ops = {
      .send = hosted_hci_bluedroid_send,
      .check_send_available = hosted_hci_bluedroid_check_send_available,
      .register_host_callback = hosted_hci_bluedroid_register_host_callback,
  };
  esp_bluedroid_attach_hci_driver(&hci_ops);

  // Initialize Bluedroid stack
  ret = esp_bluedroid_init();
  if (ret != ESP_OK) {
    ESP_LOGE(BT_TAG, "Failed to init Bluedroid: %s", esp_err_to_name(ret));
    return ret;
  }

  // Enable Bluedroid
  ret = esp_bluedroid_enable();
  if (ret != ESP_OK) {
    ESP_LOGE(BT_TAG, "Failed to enable Bluedroid: %s", esp_err_to_name(ret));
    return ret;
  }

  // Register BLE GAP callback
  ret = esp_ble_gap_register_callback(bt_gap_ble_cb);
  if (ret != ESP_OK) {
    ESP_LOGE(BT_TAG, "Failed to register BLE GAP callback: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Set device name - "Reptile Panel"
  esp_ble_gap_set_device_name("Reptile Panel");

  // Configure BLE advertising parameters
  esp_ble_adv_params_t adv_params = {
      .adv_int_min = 0x20,      // 20ms minimum interval
      .adv_int_max = 0x40,      // 40ms maximum interval
      .adv_type = ADV_TYPE_IND, // Connectable undirected advertising
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .channel_map = ADV_CHNL_ALL,
      .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };

  // Configure advertising data
  esp_ble_adv_data_t adv_data = {
      .set_scan_rsp = false,
      .include_name = true,
      .include_txpower = true,
      .min_interval = 0x0006, // 7.5ms
      .max_interval = 0x0010, // 20ms
      .appearance = 0x00,
      .manufacturer_len = 0,
      .p_manufacturer_data = NULL,
      .service_data_len = 0,
      .p_service_data = NULL,
      .service_uuid_len = 0,
      .p_service_uuid = NULL,
      .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
  };

  // Set advertising data
  ret = esp_ble_gap_config_adv_data(&adv_data);
  if (ret != ESP_OK) {
    ESP_LOGW(BT_TAG, "Failed to config adv data: %s", esp_err_to_name(ret));
  }

  // Start advertising (will be visible to other devices)
  ret = esp_ble_gap_start_advertising(&adv_params);
  if (ret != ESP_OK) {
    ESP_LOGW(BT_TAG, "Failed to start advertising: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(BT_TAG, "BLE Advertising started - Device name: 'Reptile Panel'");
  }

  bt_initialized = true;
  ESP_LOGI(BT_TAG, "Bluetooth initialized successfully");
  return ESP_OK;
}

// Start BLE scan
static esp_err_t bluetooth_start_scan(uint32_t duration_sec) {
  if (!bt_initialized) {
    ESP_LOGW(BT_TAG, "Bluetooth not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Always try to stop first if scanning
  if (bt_scanning) {
    ESP_LOGI(BT_TAG, "Stopping ongoing scan before restart...");
    esp_ble_gap_stop_scanning();
    bt_scanning = false;
    vTaskDelay(pdMS_TO_TICKS(200)); // Wait for scan to fully stop
  }

  // Clear previous scan results
  memset(bt_scan_results, 0, sizeof(bt_scan_results));
  bt_scan_count = 0;

  // Configure scan parameters
  esp_ble_scan_params_t scan_params = {
      .scan_type = BLE_SCAN_TYPE_ACTIVE,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
      .scan_interval = 0x50, // 50ms
      .scan_window = 0x30,   // 30ms
      .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
  };

  esp_err_t ret = esp_ble_gap_set_scan_params(&scan_params);
  if (ret != ESP_OK) {
    ESP_LOGE(BT_TAG, "Failed to set scan params: %s", esp_err_to_name(ret));
    return ret;
  }

  // Start scanning
  ret = esp_ble_gap_start_scanning(duration_sec);
  if (ret != ESP_OK) {
    ESP_LOGE(BT_TAG, "Failed to start scan: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(BT_TAG, "BLE scan started for %lu seconds", duration_sec);
  return ESP_OK;
}

// Stop BLE scan
static esp_err_t bluetooth_stop_scan(void) {
  if (!bt_initialized || !bt_scanning) {
    return ESP_OK;
  }

  return esp_ble_gap_stop_scanning();
}

#else // CONFIG_BT_ENABLED not defined

// Stub function when Bluetooth is disabled
static esp_err_t bluetooth_init(void) {
  ESP_LOGW("BLUETOOTH", "Bluetooth disabled in sdkconfig - skipping init");
  return ESP_ERR_NOT_SUPPORTED;
}

#endif // CONFIG_BT_ENABLED

// ESP32-C6 OTA UPDATE - REMOVED (update already done to v2.8.5)
// The C6 co-processor firmware was updated separately

// ====================================================================================
// FRENCH AZERTY KEYBOARD LAYOUT - Based on LVGL official example
// ====================================================================================

// Lowercase AZERTY layout
static const char *kb_map_azerty_lower[] = {"1",
                                            "2",
                                            "3",
                                            "4",
                                            "5",
                                            "6",
                                            "7",
                                            "8",
                                            "9",
                                            "0",
                                            LV_SYMBOL_BACKSPACE,
                                            "\n",
                                            "a",
                                            "z",
                                            "e",
                                            "r",
                                            "t",
                                            "y",
                                            "u",
                                            "i",
                                            "o",
                                            "p",
                                            "\n",
                                            "q",
                                            "s",
                                            "d",
                                            "f",
                                            "g",
                                            "h",
                                            "j",
                                            "k",
                                            "l",
                                            "m",
                                            LV_SYMBOL_NEW_LINE,
                                            "\n",
                                            "ABC",
                                            "w",
                                            "x",
                                            "c",
                                            "v",
                                            "b",
                                            "n",
                                            ",",
                                            ".",
                                            "?",
                                            "\n",
                                            "1#",
                                            LV_SYMBOL_LEFT,
                                            " ",
                                            " ",
                                            " ",
                                            LV_SYMBOL_RIGHT,
                                            LV_SYMBOL_OK,
                                            ""};

// Uppercase AZERTY layout
static const char *kb_map_azerty_upper[] = {"!",
                                            "@",
                                            "#",
                                            "$",
                                            "%",
                                            "^",
                                            "&",
                                            "*",
                                            "(",
                                            ")",
                                            LV_SYMBOL_BACKSPACE,
                                            "\n",
                                            "A",
                                            "Z",
                                            "E",
                                            "R",
                                            "T",
                                            "Y",
                                            "U",
                                            "I",
                                            "O",
                                            "P",
                                            "\n",
                                            "Q",
                                            "S",
                                            "D",
                                            "F",
                                            "G",
                                            "H",
                                            "J",
                                            "K",
                                            "L",
                                            "M",
                                            LV_SYMBOL_NEW_LINE,
                                            "\n",
                                            "abc",
                                            "W",
                                            "X",
                                            "C",
                                            "V",
                                            "B",
                                            "N",
                                            ";",
                                            ":",
                                            "!",
                                            "\n",
                                            "1#",
                                            LV_SYMBOL_LEFT,
                                            " ",
                                            " ",
                                            " ",
                                            LV_SYMBOL_RIGHT,
                                            LV_SYMBOL_OK,
                                            ""};

// Special characters layout
static const char *kb_map_special[] = {"1",
                                       "2",
                                       "3",
                                       "4",
                                       "5",
                                       "6",
                                       "7",
                                       "8",
                                       "9",
                                       "0",
                                       LV_SYMBOL_BACKSPACE,
                                       "\n",
                                       "+",
                                       "-",
                                       "*",
                                       "/",
                                       "=",
                                       "_",
                                       "<",
                                       ">",
                                       "[",
                                       "]",
                                       "\n",
                                       "{",
                                       "}",
                                       "|",
                                       "\\",
                                       "~",
                                       "`",
                                       "'",
                                       "\"",
                                       ":",
                                       ";",
                                       LV_SYMBOL_NEW_LINE,
                                       "\n",
                                       "abc",
                                       "@",
                                       "#",
                                       "$",
                                       "%",
                                       "^",
                                       "&",
                                       ",",
                                       ".",
                                       "?",
                                       "\n",
                                       "ABC",
                                       LV_SYMBOL_LEFT,
                                       " ",
                                       " ",
                                       " ",
                                       LV_SYMBOL_RIGHT,
                                       LV_SYMBOL_OK,
                                       ""};

// Control map for keyboard buttons (defines button widths and special flags)
// LVGL 9 uses LV_KEYBOARD_CTRL_BTN_FLAGS for buttons that should change mode
// Row 1: 11 buttons (numbers + backspace)
// Row 2: 10 buttons (letters a-p)
// Row 3: 11 buttons (letters q-m + enter)
// Row 4: 10 buttons (shift + letters + punctuation)
// Row 5: 7 buttons (123 + arrows + spaces + OK)

// Flag to mark mode-switching buttons
#define KB_CTRL_MODE_BTN                                                       \
  (LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_NO_REPEAT |             \
   LV_BUTTONMATRIX_CTRL_CLICK_TRIG)

static const lv_buttonmatrix_ctrl_t kb_ctrl_lower[] = {
    // Row 1: numbers + backspace (wider)
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
    // Row 2: letters
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    // Row 3: letters + enter (wider)
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
    // Row 4: ABC (mode switch) + letters + punctuation
    6 | KB_CTRL_MODE_BTN, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    // Row 5: 123 (mode switch) + arrows + spaces + OK
    5 | KB_CTRL_MODE_BTN, 3, 7, 7, 7, 3, 5 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG};

static const lv_buttonmatrix_ctrl_t kb_ctrl_upper[] = {
    // Row 1: numbers + backspace
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
    // Row 2: letters
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    // Row 3: letters + enter
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
    // Row 4: abc (mode switch) + letters
    6 | KB_CTRL_MODE_BTN, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    // Row 5: 123 + arrows + spaces + OK
    5 | KB_CTRL_MODE_BTN, 3, 7, 7, 7, 3, 5 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG};

static const lv_buttonmatrix_ctrl_t kb_ctrl_special[] = {
    // Row 1: numbers + backspace
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
    // Row 2: special chars
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    // Row 3: special chars + enter
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
    // Row 4: abc (mode switch) + special chars
    6 | KB_CTRL_MODE_BTN, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    // Row 5: ABC + arrows + spaces + OK
    5 | KB_CTRL_MODE_BTN, 3, 7, 7, 7, 3, 5 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG};

// ====================================================================================
// HARDWARE FUNCTIONS
// ====================================================================================

static esp_err_t enable_dsi_phy_power(void) {
  static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
  if (phy_pwr_chan)
    return ESP_OK;
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = DSI_PHY_LDO_CHANNEL,
      .voltage_mv = DSI_PHY_VOLTAGE_MV,
  };
  return esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
}

static esp_err_t backlight_init(void) {
  ledc_timer_config_t timer_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = BL_LEDC_TIMER,
      .freq_hz = BL_PWM_FREQ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

  ledc_channel_config_t ch_cfg = {
      .gpio_num = LCD_BL_GPIO,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = BL_LEDC_CHANNEL,
      .timer_sel = BL_LEDC_TIMER,
      .duty = 0,
      .hpoint = 0,
  };
  return ledc_channel_config(&ch_cfg);
}

static void backlight_set(uint8_t percent) {
  if (percent > 100)
    percent = 100;
  current_brightness = percent;
  uint32_t duty = (percent * 1023) / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

static esp_err_t i2c_init(void) {
  i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = TOUCH_I2C_SDA,
      .scl_io_num = TOUCH_I2C_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = false,
  };
  return i2c_new_master_bus(&bus_config, &i2c_bus_handle);
}

static esp_err_t touch_init(void) {
  if (!i2c_bus_handle)
    ESP_ERROR_CHECK(i2c_init());

  esp_lcd_panel_io_handle_t touch_io = NULL;
  esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = 0x5D,
      .scl_speed_hz = TOUCH_I2C_FREQ_HZ,
      .control_phase_bytes = 1,
      .lcd_cmd_bits = 16,
      .lcd_param_bits = 0,
      .dc_bit_offset = 0,
      .flags = {.disable_control_phase = 1},
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_io_i2c(i2c_bus_handle, &io_config, &touch_io));

  esp_lcd_touch_config_t touch_cfg = {
      .x_max = LCD_H_RES,
      .y_max = LCD_V_RES,
      .rst_gpio_num = GPIO_NUM_NC,
      .int_gpio_num = GPIO_NUM_NC,
      .levels = {.reset = 0, .interrupt = 0},
      .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
  };
  return esp_lcd_touch_new_i2c_gt911(touch_io, &touch_cfg, &touch_handle);
}

static esp_err_t display_init(esp_lcd_panel_io_handle_t *out_io,
                              esp_lcd_panel_handle_t *out_panel) {
  ESP_ERROR_CHECK(enable_dsi_phy_power());
  vTaskDelay(pdMS_TO_TICKS(10));

  esp_lcd_dsi_bus_handle_t dsi_bus;
  esp_lcd_dsi_bus_config_t bus_cfg = {
      .bus_id = 0,
      .num_data_lanes = DSI_LANE_NUM,
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = DSI_LANE_BITRATE,
  };
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));
  vTaskDelay(pdMS_TO_TICKS(50));

  esp_lcd_panel_io_handle_t panel_io;
  esp_lcd_dbi_io_config_t dbi_cfg = {
      .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &panel_io));

  esp_lcd_dpi_panel_config_t dpi_cfg = {
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = DPI_CLOCK_MHZ,
      .virtual_channel = 0,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 1,
      .video_timing =
          {
              .h_size = LCD_H_RES,
              .v_size = LCD_V_RES,
              .hsync_pulse_width = 12,
              .hsync_back_porch = 42,
              .hsync_front_porch = 42,
              .vsync_pulse_width = 2,
              .vsync_back_porch = 8,
              .vsync_front_porch = 166,
          },
  };

  st7701_vendor_config_t vendor_cfg = {
      .flags.use_mipi_interface = 1,
      .mipi_config = {.dsi_bus = dsi_bus, .dpi_config = &dpi_cfg},
      .init_cmds = LCD_INIT_CMDS,
      .init_cmds_size = LCD_INIT_CMDS_SIZE,
  };

  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = LCD_RST_GPIO,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
      .vendor_config = &vendor_cfg,
  };

  esp_lcd_panel_handle_t panel;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(panel_io, &panel_cfg, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  vTaskDelay(pdMS_TO_TICKS(50));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

  *out_io = panel_io;
  *out_panel = panel;
  ESP_LOGI(TAG, "Display initialized");
  return ESP_OK;
}

// ====================================================================================
// UI HELPER FUNCTIONS
// ====================================================================================

static lv_obj_t *create_card(lv_obj_t *parent, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);

  // Glassmorphism effect - semi-transparent background
  lv_obj_set_style_bg_color(card, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);

  // Rounded corners
  lv_obj_set_style_radius(card, 16, 0);

  // Subtle border
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, COLOR_BORDER, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_70, 0);

  // Inner padding
  lv_obj_set_style_pad_all(card, 12, 0);

  // Shadow for depth
  lv_obj_set_style_shadow_width(card, 15, 0);
  lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
  lv_obj_set_style_shadow_offset_y(card, 3, 0);

  // Pressed state - just change colors, no transform (causes touch issues)
  lv_obj_set_style_bg_color(card, COLOR_BG_CARD_HOVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(card, COLOR_PRIMARY, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 2, LV_STATE_PRESSED);

  // Disable scrolling (cards don't scroll)
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  return card;
}

// Create a premium button
static lv_obj_t *create_button(lv_obj_t *parent, const char *text, int w,
                               int h) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, w, h);

  // Primary color background
  lv_obj_set_style_bg_color(btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

  // Rounded
  lv_obj_set_style_radius(btn, 12, 0);

  // No border
  lv_obj_set_style_border_width(btn, 0, 0);

  // Subtle shadow
  lv_obj_set_style_shadow_width(btn, 10, 0);
  lv_obj_set_style_shadow_color(btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);

  // Pressed state - darker color only
  lv_obj_set_style_bg_color(btn, COLOR_PRIMARY_DARK, LV_STATE_PRESSED);

  // Label
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, COLOR_BG_DARK, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl);

  return btn;
}

// Forward declarations for page creators
static void create_home_page(lv_obj_t *parent);
static void create_settings_page(lv_obj_t *parent);
static void create_wifi_page(lv_obj_t *parent);
static void create_bluetooth_page(lv_obj_t *parent);
static void create_gallery_page(lv_obj_t *parent);
static void create_animals_page(lv_obj_t *parent);
static void create_animal_detail_page(lv_obj_t *parent);
static void create_breeding_page(lv_obj_t *parent);
static void create_conformity_page(lv_obj_t *parent);

// Forward declarations for update functions used in navigate_to
static void gallery_scan_images(void);
static void gallery_update_display(void);
static void update_animal_list(void);
static void update_animal_detail(void);

// Current page tracking
typedef enum {
  PAGE_HOME,
  PAGE_SETTINGS,
  PAGE_WIFI,
  PAGE_BLUETOOTH,
  PAGE_GALLERY,
  PAGE_ANIMALS,
  PAGE_ANIMAL_DETAIL,
  PAGE_BREEDING,
  PAGE_CONFORMITY, // Registre, exports, attestations
  PAGE_DIAGNOSTICS // Self-test hardware
} page_id_t;

static page_id_t current_page = PAGE_HOME;

// Delete all content pages (keeps status bar and navbar)
static void delete_all_pages(void) {
  if (page_home) {
    lv_obj_del(page_home);
    page_home = NULL;
    dashboard_snake_count = NULL;
    dashboard_lizard_count = NULL;
    dashboard_turtle_count = NULL;
    dashboard_alerts_label = NULL;
  }
  if (page_settings) {
    lv_obj_del(page_settings);
    page_settings = NULL;
  }
  if (page_wifi) {
    lv_obj_del(page_wifi);
    page_wifi = NULL;
    wifi_list = NULL;
    wifi_keyboard = NULL;
    wifi_password_ta = NULL;
    wifi_status_label = NULL;
    wifi_ssid_label = NULL;
    wifi_pwd_container = NULL;
  }
  if (page_bluetooth) {
    lv_obj_del(page_bluetooth);
    page_bluetooth = NULL;
    bt_list = NULL;
    bt_status_label = NULL;
    bt_device_label = NULL;
  }
  if (page_gallery) {
    lv_obj_del(page_gallery);
    page_gallery = NULL;
    gallery_image = NULL;
    gallery_filename_label = NULL;
    gallery_index_label = NULL;
  }
  if (page_animals) {
    lv_obj_del(page_animals);
    page_animals = NULL;
    animal_list = NULL;
  }
  if (page_animal_detail) {
    lv_obj_del(page_animal_detail);
    page_animal_detail = NULL;
    detail_name_label = NULL;
    detail_info_label = NULL;
  }
  if (page_breeding) {
    lv_obj_del(page_breeding);
    page_breeding = NULL;
  }
  // Note: page_calendar might not exist
  if (page_calendar) {
    lv_obj_del(page_calendar);
    page_calendar = NULL;
  }
  if (page_conformity) {
    lv_obj_del(page_conformity);
    page_conformity = NULL;
    conformity_status_label = NULL;
  }
}

static void navigate_to(page_id_t target_page) {
  ESP_LOGI(TAG, "navigate_to: %d -> %d", current_page, target_page);

  lv_obj_t *scr = lv_scr_act();

  // Delete all existing content pages
  delete_all_pages();

  // Create only the requested page
  switch (target_page) {
  case PAGE_HOME:
    create_home_page(scr);
    current_page = PAGE_HOME;
    break;
  case PAGE_SETTINGS:
    create_settings_page(scr);
    current_page = PAGE_SETTINGS;
    break;
  case PAGE_WIFI:
    create_wifi_page(scr);
    current_page = PAGE_WIFI;
    break;
  case PAGE_BLUETOOTH:
    create_bluetooth_page(scr);
    current_page = PAGE_BLUETOOTH;
    break;
  case PAGE_GALLERY:
    create_gallery_page(scr);
    gallery_scan_images();
    gallery_current_index = 0;
    gallery_update_display();
    current_page = PAGE_GALLERY;
    break;
  case PAGE_ANIMALS:
    create_animals_page(scr);
    update_animal_list();
    current_page = PAGE_ANIMALS;
    break;
  case PAGE_ANIMAL_DETAIL:
    create_animal_detail_page(scr);
    update_animal_detail();
    current_page = PAGE_ANIMAL_DETAIL;
    break;
  case PAGE_BREEDING:
    create_breeding_page(scr);
    current_page = PAGE_BREEDING;
    break;
  case PAGE_CONFORMITY:
    create_conformity_page(scr);
    current_page = PAGE_CONFORMITY;
    break;
  case PAGE_DIAGNOSTICS:
    // Not implemented
    break;
  }

  // Bring navbar and status bar back to front (z-order fix)
  if (ui_status_bar) {
    lv_obj_move_foreground(ui_status_bar);
  }
  if (ui_navbar) {
    lv_obj_move_foreground(ui_navbar);
  }

  ESP_LOGI(TAG, "  Page created and active");
}

// Legacy show_page function for compatibility
static void show_page(lv_obj_t *page) {
  // Determine which page was requested and navigate
  if (page == page_home || page == NULL) {
    navigate_to(PAGE_HOME);
  } else if (page == page_settings) {
    navigate_to(PAGE_SETTINGS);
  } else if (page == page_wifi) {
    navigate_to(PAGE_WIFI);
  } else if (page == page_bluetooth) {
    navigate_to(PAGE_BLUETOOTH);
  } else if (page == page_gallery) {
    navigate_to(PAGE_GALLERY);
  } else if (page == page_animals) {
    navigate_to(PAGE_ANIMALS);
  } else if (page == page_animal_detail) {
    navigate_to(PAGE_ANIMAL_DETAIL);
  } else if (page == page_breeding) {
    navigate_to(PAGE_BREEDING);
  } else {
    // Unknown page, go home
    navigate_to(PAGE_HOME);
  }
}

// ====================================================================================
// EVENT CALLBACKS
// ====================================================================================

static void nav_home_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_HOME);
}

static void nav_settings_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_SETTINGS);
}

static void brightness_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  backlight_set((uint8_t)lv_slider_get_value(slider));
}

static void wifi_toggle_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  bool enable = lv_obj_has_state(sw, LV_STATE_CHECKED);

  if (enable) {
    wifi_start();
  } else {
    wifi_stop();
  }

  if (icon_wifi) {
    lv_obj_set_style_text_color(
        icon_wifi,
        wifi_enabled ? (wifi_connected ? COLOR_SUCCESS : COLOR_WARNING)
                     : COLOR_TEXT_DIM,
        0);
  }
  ESP_LOGI(TAG, "WiFi %s", wifi_enabled ? "enabled" : "disabled");
}

static void bluetooth_toggle_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  bool enable = lv_obj_has_state(sw, LV_STATE_CHECKED);
  bluetooth_enabled = enable;

  if (enable) {
#if CONFIG_BT_ENABLED
    // Initialize Bluetooth if not already done
    if (!bt_initialized) {
      esp_err_t ret = bluetooth_init();
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Bluetooth: %s",
                 esp_err_to_name(ret));
      }
    }
#endif
    // Navigate to Bluetooth configuration page
    navigate_to(PAGE_BLUETOOTH);
  }

  if (icon_bluetooth) {
    lv_obj_set_style_text_color(
        icon_bluetooth, bluetooth_enabled ? COLOR_PRIMARY : COLOR_TEXT_DIM, 0);
  }
  ESP_LOGI(TAG, "Bluetooth %s", bluetooth_enabled ? "enabled" : "disabled");
}

// WiFi Page Callbacks
static void wifi_scan_btn_cb(lv_event_t *e);
static void wifi_list_cb(lv_event_t *e);
static void wifi_connect_btn_cb(lv_event_t *e);
static void wifi_back_btn_cb(lv_event_t *e);
static void wifi_keyboard_ready_cb(lv_event_t *e);
// Note: Mode switching (ABC/abc/1#) is handled automatically by LVGL's default
// keyboard handler

static void update_wifi_list(void) {
  if (!wifi_list)
    return;
  lv_obj_clean(wifi_list);

  // Limit displayed networks to avoid memory issues
  int display_count = wifi_scan_count > 8 ? 8 : wifi_scan_count;

  for (int i = 0; i < display_count; i++) {
    if (wifi_scan_results[i].ssid[0] == '\0')
      continue;

    // Create button manually (not using lv_list)
    lv_obj_t *btn = lv_btn_create(wifi_list);
    lv_obj_set_size(btn, lv_pct(100), 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x42A5F5), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, wifi_list_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);

    // WiFi icon + SSID label
    lv_obj_t *label = lv_label_create(btn);
    char label_text[48];
    snprintf(label_text, sizeof(label_text), LV_SYMBOL_WIFI " %s",
             (const char *)wifi_scan_results[i].ssid);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);

    // RSSI indicator
    int rssi = wifi_scan_results[i].rssi;
    lv_obj_t *rssi_label = lv_label_create(btn);
    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
    lv_label_set_text(rssi_label, rssi_str);
    lv_obj_set_style_text_color(rssi_label,
                                rssi > -60   ? lv_color_hex(0x4CAF50)
                                : rssi > -75 ? lv_color_hex(0xFFEB3B)
                                             : lv_color_hex(0xFF5252),
                                0);
    lv_obj_align(rssi_label, LV_ALIGN_RIGHT_MID, -5, 0);
  }
  lv_obj_invalidate(wifi_list);
}

static void wifi_scan_btn_cb(lv_event_t *e) {
  (void)e;
  if (wifi_status_label) {
    lv_label_set_text(wifi_status_label, "Scan en cours...");
  }

  // Perform scan - this will block for ~8-10 seconds
  // During this time, the UI will be frozen but that's acceptable
  esp_err_t ret = wifi_scan();

  // Update the list
  if (wifi_list) {
    lv_obj_clean(wifi_list);

    // Limit displayed networks
    int display_count = wifi_scan_count > 8 ? 8 : wifi_scan_count;
    ESP_LOGI(WIFI_TAG, "Updating WiFi list with %d networks", display_count);

    for (int i = 0; i < display_count; i++) {
      // Skip if SSID is empty
      if (wifi_scan_results[i].ssid[0] == '\0')
        continue;

      // Create button manually (not using lv_list)
      lv_obj_t *btn = lv_btn_create(wifi_list);
      lv_obj_set_size(btn, lv_pct(100), 40);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x42A5F5), LV_STATE_PRESSED);
      lv_obj_set_style_radius(btn, 8, 0);
      lv_obj_add_event_cb(btn, wifi_list_cb, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);

      // WiFi icon + SSID label
      lv_obj_t *label = lv_label_create(btn);
      char label_text[48];
      snprintf(label_text, sizeof(label_text), LV_SYMBOL_WIFI " %s",
               (const char *)wifi_scan_results[i].ssid);
      lv_label_set_text(label, label_text);
      lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
      lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);

      // RSSI indicator
      int rssi = wifi_scan_results[i].rssi;
      lv_obj_t *rssi_label = lv_label_create(btn);
      char rssi_str[16];
      snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
      lv_label_set_text(rssi_label, rssi_str);
      lv_obj_set_style_text_color(rssi_label,
                                  rssi > -60   ? lv_color_hex(0x4CAF50)
                                  : rssi > -75 ? lv_color_hex(0xFFEB3B)
                                               : lv_color_hex(0xFF5252),
                                  0);
      lv_obj_align(rssi_label, LV_ALIGN_RIGHT_MID, -5, 0);

      ESP_LOGI(WIFI_TAG, "  Added network: %s",
               (const char *)wifi_scan_results[i].ssid);
    }

    // Force container to refresh
    lv_obj_invalidate(wifi_list);
  } else {
    ESP_LOGW(WIFI_TAG, "wifi_list is NULL - cannot update!");
  }

  if (wifi_status_label) {
    char status[64];
    if (ret == ESP_OK) {
      snprintf(status, sizeof(status), "Trouve: %d reseaux", wifi_scan_count);
      lv_obj_set_style_text_color(wifi_status_label, COLOR_SUCCESS, 0);
    } else {
      snprintf(status, sizeof(status), "Erreur: %s", esp_err_to_name(ret));
      lv_obj_set_style_text_color(wifi_status_label, COLOR_DANGER, 0);
    }
    lv_label_set_text(wifi_status_label, status);
  }

  // Invalidate entire page to force redraw (lv_refr_now can deadlock)
  if (page_wifi) {
    lv_obj_invalidate(page_wifi);
  }
  ESP_LOGI(WIFI_TAG, "WiFi list update complete");
}

static void wifi_list_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx >= 0 && idx < wifi_scan_count) {
    snprintf(wifi_selected_ssid, sizeof(wifi_selected_ssid), "%s",
             (const char *)wifi_scan_results[idx].ssid);

    if (wifi_ssid_label) {
      char ssid_text[64];
      snprintf(ssid_text, sizeof(ssid_text), "Network: %s", wifi_selected_ssid);
      lv_label_set_text(wifi_ssid_label, ssid_text);
    }

    // Show password container and keyboard
    if (wifi_pwd_container) {
      lv_obj_clear_flag(wifi_pwd_container, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_password_ta) {
      lv_textarea_set_text(wifi_password_ta, "");
      lv_textarea_set_password_mode(wifi_password_ta,
                                    true); // Reset to password mode
    }
    if (wifi_keyboard) {
      lv_obj_clear_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "Selected network: %s", wifi_selected_ssid);
  }
}

// Custom keyboard ready event - triggered when OK button is pressed
static void wifi_keyboard_ready_cb(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_target(e);
  (void)kb;

  // Handle OK button - connect to WiFi
  if (wifi_password_ta) {
    const char *pwd = lv_textarea_get_text(wifi_password_ta);
    snprintf(wifi_password_input, sizeof(wifi_password_input), "%s", pwd);
  }
  if (strlen(wifi_selected_ssid) > 0) {
    wifi_connect_to(wifi_selected_ssid, wifi_password_input);

    if (wifi_status_label) {
      lv_label_set_text(wifi_status_label, "Connecting...");
    }
  }

  // Hide keyboard and password container
  if (wifi_keyboard) {
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
  }
  if (wifi_pwd_container) {
    lv_obj_add_flag(wifi_pwd_container, LV_OBJ_FLAG_HIDDEN);
  }
}

// Note: Mode switching (ABC/abc/1#) is now handled automatically by LVGL's
// default keyboard handler. The button texts "ABC", "abc", "1#" are recognized
// and trigger mode changes without needing a custom handler.

static void wifi_connect_btn_cb(lv_event_t *e) {
  (void)e;
  if (wifi_password_ta) {
    const char *pwd = lv_textarea_get_text(wifi_password_ta);
    snprintf(wifi_password_input, sizeof(wifi_password_input), "%s", pwd);
  }

  if (strlen(wifi_selected_ssid) > 0) {
    wifi_connect_to(wifi_selected_ssid, wifi_password_input);
    if (wifi_status_label) {
      lv_label_set_text(wifi_status_label, "Connecting...");
    }
  }
}

// Toggle password visibility
static void wifi_password_toggle_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  if (wifi_password_ta) {
    bool is_password = lv_textarea_get_password_mode(wifi_password_ta);
    lv_textarea_set_password_mode(wifi_password_ta, !is_password);

    // Update eye icon
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
      lv_label_set_text(lbl,
                        is_password ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    }
  }
}

static void wifi_back_btn_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_SETTINGS);
}

// Forget saved WiFi network
static void wifi_forget_btn_cb(lv_event_t *e) {
  (void)e;
  ESP_LOGI(WIFI_TAG, "Forgetting saved WiFi network...");

  // Disconnect first if connected
  if (wifi_connected) {
    esp_wifi_disconnect();
    wifi_connected = false;
  }

  // Delete saved credentials
  wifi_delete_credentials();

  // Reset UI
  if (lvgl_port_lock(10)) {
    if (wifi_status_label) {
      lv_label_set_text(wifi_status_label,
                        "Reseau oublie. Scannez pour reconnecter.");
    }
    if (wifi_ssid_label) {
      lv_label_set_text(wifi_ssid_label, "Network: (none selected)");
    }
    if (icon_wifi) {
      lv_obj_set_style_text_color(icon_wifi, COLOR_TEXT_DIM, 0);
    }
    lvgl_port_unlock();
  }

  memset(wifi_selected_ssid, 0, sizeof(wifi_selected_ssid));
  memset(wifi_ip, 0, sizeof(wifi_ip));
  ESP_LOGI(WIFI_TAG, "WiFi network forgotten");
}

// Disconnect from current WiFi
static void wifi_disconnect_btn_cb(lv_event_t *e) {
  (void)e;
  ESP_LOGI(WIFI_TAG, "Disconnecting from WiFi...");

  esp_wifi_disconnect();
  wifi_connected = false;

  if (lvgl_port_lock(10)) {
    if (wifi_status_label) {
      lv_label_set_text(wifi_status_label,
                        "Deconnecte. Scannez pour reconnecter.");
    }
    if (icon_wifi) {
      lv_obj_set_style_text_color(icon_wifi, COLOR_TEXT_DIM, 0);
    }
    lvgl_port_unlock();
  }

  memset(wifi_ip, 0, sizeof(wifi_ip));
  ESP_LOGI(WIFI_TAG, "WiFi disconnected");
}

static void nav_wifi_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_WIFI);
}

// ====================================================================================
// BLUETOOTH PAGE CALLBACKS
// ====================================================================================

// Forward declarations for Bluetooth callbacks
static void bt_scan_btn_cb(lv_event_t *e);
static void bt_list_cb(lv_event_t *e);
static void bt_back_btn_cb(lv_event_t *e);

#if CONFIG_BT_ENABLED
// Update Bluetooth device list from scan results
// Show up to 10 devices
#define BT_MAX_DISPLAY_DEVICES 10

static void update_bt_list(void) {
  if (!bt_list)
    return;
  lv_obj_clean(bt_list);

  int displayed = 0;
  for (int i = 0; i < bt_scan_count && i < BT_SCAN_MAX_DEVICES &&
                  displayed < BT_MAX_DISPLAY_DEVICES;
       i++) {
    if (!bt_scan_results[i].valid)
      continue;

    // Create list item with device name AND MAC address for identification
    char item_text[80];
    char bda_str[18];
    bda_to_str(bt_scan_results[i].bda, bda_str, sizeof(bda_str));

    // Show name + MAC, or just MAC if name is unknown
    if (strcmp(bt_scan_results[i].name, "(Unknown)") == 0 ||
        strlen(bt_scan_results[i].name) == 0) {
      snprintf(item_text, sizeof(item_text), "Inconnu (%s)", bda_str);
    } else {
      const char *short_mac =
          strlen(bda_str) > 8 ? bda_str + strlen(bda_str) - 8 : bda_str;
      snprintf(item_text, sizeof(item_text), "%s (...%s)",
               bt_scan_results[i].name, short_mac);
    }

    // Create button manually (not using lv_list)
    lv_obj_t *btn = lv_btn_create(bt_list);
    lv_obj_set_size(btn, lv_pct(100), 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x6A1B9A), 0); // Violet
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xAB47BC), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, bt_list_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    // BT icon + device name
    lv_obj_t *label = lv_label_create(btn);
    char label_text[96];
    snprintf(label_text, sizeof(label_text), LV_SYMBOL_BLUETOOTH " %s",
             item_text);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);

    // RSSI indicator
    int rssi = bt_scan_results[i].rssi;
    lv_obj_t *rssi_label = lv_label_create(btn);
    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
    lv_label_set_text(rssi_label, rssi_str);
    lv_obj_set_style_text_color(rssi_label,
                                rssi > -60   ? lv_color_hex(0x4CAF50)
                                : rssi > -80 ? lv_color_hex(0xFFEB3B)
                                             : lv_color_hex(0xFF5252),
                                0);
    lv_obj_align(rssi_label, LV_ALIGN_RIGHT_MID, -5, 0);

    displayed++;
    ESP_LOGI(BT_TAG, "  Added BT device: %s", item_text);
  }

  lv_obj_invalidate(bt_list);
}

// Timer callback to update BT list after scan
static void bt_scan_timer_cb(lv_timer_t *timer) {
  // Use LVGL lock for thread safety
  if (!lvgl_port_lock(100)) {
    ESP_LOGW(BT_TAG, "Could not acquire LVGL lock for BT list update");
    lv_timer_delete(timer);
    return;
  }

  // Update the list with results
  update_bt_list();

  if (bt_status_label) {
    char status[64];
    snprintf(status, sizeof(status), "%d appareils BLE trouves", bt_scan_count);
    lv_label_set_text(bt_status_label, status);
  }

  lvgl_port_unlock();

  // Delete the timer
  lv_timer_delete(timer);
}
#endif

static void bt_scan_btn_cb(lv_event_t *e) {
  (void)e;
  if (bt_status_label) {
    lv_label_set_text(bt_status_label, "Recherche des appareils BLE...");
  }

#if CONFIG_BT_ENABLED
  // Start BLE scan (10 seconds) - will auto-stop if one is in progress
  // UI will be updated from main loop when bt_scan_update_pending becomes true
  esp_err_t ret = bluetooth_start_scan(10);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BLE scan failed: %s", esp_err_to_name(ret));
    if (bt_status_label) {
      lv_label_set_text(bt_status_label, "Echec - reessayez");
    }
    return;
  }
  // No timer needed - main loop checks bt_scan_update_pending flag
#else
  // BT disabled in config
  if (bt_status_label) {
    lv_label_set_text(bt_status_label, "Bluetooth desactive");
  }
#endif
}

static void bt_list_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
#if CONFIG_BT_ENABLED
  if (idx >= 0 && idx < bt_scan_count && bt_scan_results[idx].valid) {
    bt_selected_device_idx = idx; // Store selected device

    if (bt_device_label) {
      char info[128];
      char bda_str[18];
      bda_to_str(bt_scan_results[idx].bda, bda_str, sizeof(bda_str));
      snprintf(info, sizeof(info), "Appareil: %s\nMAC: %s\nRSSI: %d dBm",
               bt_scan_results[idx].name, bda_str, bt_scan_results[idx].rssi);
      lv_label_set_text(bt_device_label, info);
    }
    ESP_LOGI(TAG, "Selected BLE device [%d]: %s", idx,
             bt_scan_results[idx].name);
  }
#else
  (void)idx;
  if (bt_device_label) {
    lv_label_set_text(bt_device_label, "Bluetooth non disponible");
  }
#endif
}

static void bt_back_btn_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_SETTINGS);
}

static void nav_bluetooth_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_BLUETOOTH);
}

// ESP32-C6 OTA UPDATE CALLBACKS - REMOVED (update already done)

// ====================================================================================
// CREATE STATUS BAR
// ====================================================================================

// Forward declaration for conformity callback
static void nav_conformity_cb(lv_event_t *e);

static void create_status_bar(lv_obj_t *parent) {
  lv_obj_t *status_bar = lv_obj_create(parent);
  ui_status_bar = status_bar; // Store global reference
  lv_obj_set_size(status_bar, LCD_H_RES, 50);
  lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(status_bar, COLOR_HEADER, 0);
  lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(status_bar, 0, 0);
  lv_obj_set_style_radius(status_bar, 0, 0);
  lv_obj_set_style_pad_hor(status_bar, 12, 0);
  lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

  // Left: Logo from SD or fallback icon + "Smart Panel"
  lv_obj_t *logo_container = lv_obj_create(status_bar);
  lv_obj_set_size(logo_container, 180, 40);
  lv_obj_align(logo_container, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(logo_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(logo_container, 0, 0);
  lv_obj_set_style_pad_all(logo_container, 0, 0);
  lv_obj_clear_flag(logo_container, LV_OBJ_FLAG_SCROLLABLE);

  // Try to load logo from SD card
  if (sd_mounted) {
    logo_img = lv_image_create(logo_container);
    lv_image_set_src(logo_img, SD_MOUNT_POINT "/imgs/logo.png");
    lv_obj_set_size(logo_img, 32, 32);
    lv_image_set_inner_align(logo_img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_align(logo_img, LV_ALIGN_LEFT_MID, 0, 0);

    // Check if image loaded successfully
    if (lv_image_get_src(logo_img) == NULL) {
      ESP_LOGW(TAG, "Failed to load logo, using fallback");
      lv_obj_delete(logo_img);
      logo_img = NULL; // No fallback icon
    }
  } else {
    // No fallback icon
    logo_img = NULL;
  }

  lv_obj_t *title = lv_label_create(logo_container);
  lv_label_set_text(title, "Reptile Panel");
  lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, logo_img ? 38 : 0, 0);

  // Center: Alerts indicator (clickable)
  lv_obj_t *alerts_btn = lv_btn_create(status_bar);
  lv_obj_set_size(alerts_btn, 200, 36);
  lv_obj_align(alerts_btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(alerts_btn, lv_color_hex(0x1E3A5F), 0);
  lv_obj_set_style_bg_color(alerts_btn, lv_color_hex(0xFF9800),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(alerts_btn, 8, 0);
  lv_obj_set_style_border_width(alerts_btn, 1, 0);
  lv_obj_set_style_border_color(alerts_btn, lv_color_hex(0xFF9800), 0);

  lv_obj_t *alerts_icon = lv_label_create(alerts_btn);
  lv_label_set_text(alerts_icon, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(alerts_icon, lv_color_hex(0xFF9800), 0);
  lv_obj_align(alerts_icon, LV_ALIGN_LEFT_MID, 5, 0);

  dashboard_alerts_label = lv_label_create(alerts_btn);
  lv_label_set_text(dashboard_alerts_label, "0 alertes");
  lv_obj_set_style_text_color(dashboard_alerts_label, lv_color_hex(0xFFFFFF),
                              0);
  lv_obj_set_style_text_font(dashboard_alerts_label, &lv_font_montserrat_12, 0);
  lv_obj_align(dashboard_alerts_label, LV_ALIGN_LEFT_MID, 30, 0);

  // Make alerts button open conformity/alerts page
  lv_obj_add_event_cb(alerts_btn, nav_conformity_cb, LV_EVENT_CLICKED, NULL);

  // Right: Date, Time, BT, WiFi
  lv_obj_t *right_container = lv_obj_create(status_bar);
  lv_obj_set_size(right_container, 260, 40);
  lv_obj_align(right_container, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(right_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(right_container, 0, 0);
  lv_obj_set_style_pad_all(right_container, 0, 0);
  lv_obj_clear_flag(right_container, LV_OBJ_FLAG_SCROLLABLE);

  label_date = lv_label_create(right_container);
  lv_label_set_text(label_date, "01 Jan");
  lv_obj_set_style_text_color(label_date, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(label_date, &lv_font_montserrat_12, 0);
  lv_obj_align(label_date, LV_ALIGN_LEFT_MID, 0, 0);

  label_time = lv_label_create(right_container);
  lv_label_set_text(label_time, "00:00");
  lv_obj_set_style_text_color(label_time, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(label_time, &lv_font_montserrat_16, 0);
  lv_obj_align(label_time, LV_ALIGN_LEFT_MID, 55, 0);

  icon_bluetooth = lv_label_create(right_container);
  lv_label_set_text(icon_bluetooth, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(
      icon_bluetooth, bluetooth_enabled ? COLOR_PRIMARY : COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(icon_bluetooth, &lv_font_montserrat_18, 0);
  lv_obj_align(icon_bluetooth, LV_ALIGN_RIGHT_MID, -60, 0);

  // Battery icon
  icon_battery = lv_label_create(right_container);
  lv_label_set_text(icon_battery, battery_get_icon(battery_get_level()));
  lv_obj_set_style_text_color(icon_battery, COLOR_SUCCESS, 0);
  lv_obj_set_style_text_font(icon_battery, &lv_font_montserrat_18, 0);
  lv_obj_align(icon_battery, LV_ALIGN_RIGHT_MID, -30, 0);

  icon_wifi = lv_label_create(right_container);
  lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(icon_wifi,
                              wifi_enabled ? COLOR_SUCCESS : COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(icon_wifi, &lv_font_montserrat_18, 0);
  lv_obj_align(icon_wifi, LV_ALIGN_RIGHT_MID, 0, 0);
}

// ====================================================================================
// CREATE NAVIGATION BAR
// ====================================================================================

// Forward declarations for navbar callbacks
static void nav_animals_cb(lv_event_t *e);
static void nav_breeding_cb(lv_event_t *e);
static void nav_gallery_cb(lv_event_t *e);
static void nav_conformity_cb(lv_event_t *e);

static void create_navbar(lv_obj_t *parent) {
  lv_obj_t *navbar = lv_obj_create(parent);
  ui_navbar = navbar; // Store global reference for z-order control
  lv_obj_set_size(navbar, LCD_H_RES, 60);
  lv_obj_align(navbar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(navbar, COLOR_HEADER, 0);
  lv_obj_set_style_bg_opa(navbar, LV_OPA_90, 0);
  lv_obj_set_style_border_width(navbar, 0, 0);
  lv_obj_set_style_radius(navbar, 0, 0);
  lv_obj_set_style_pad_all(navbar, 5, 0);
  lv_obj_clear_flag(navbar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(navbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(navbar, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // === LEFT SIDE ===

  // Animals button
  lv_obj_t *btn_animals = lv_btn_create(navbar);
  lv_obj_set_size(btn_animals, 50, 44);
  lv_obj_set_style_bg_color(btn_animals, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(btn_animals, lv_color_hex(0x4CAF50),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_animals, 12, 0);
  lv_obj_set_style_border_width(btn_animals, 1, 0);
  lv_obj_set_style_border_color(btn_animals, COLOR_BORDER, 0);
  lv_obj_t *icon_animals = lv_label_create(btn_animals);
  lv_label_set_text(icon_animals, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(icon_animals, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(icon_animals, lv_color_hex(0x4CAF50), 0);
  lv_obj_center(icon_animals);
  lv_obj_add_event_cb(btn_animals, nav_animals_cb, LV_EVENT_CLICKED, NULL);

  // Repro button
  lv_obj_t *btn_repro = lv_btn_create(navbar);
  lv_obj_set_size(btn_repro, 50, 44);
  lv_obj_set_style_bg_color(btn_repro, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(btn_repro, lv_color_hex(0xFFAB00),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_repro, 12, 0);
  lv_obj_set_style_border_width(btn_repro, 1, 0);
  lv_obj_set_style_border_color(btn_repro, COLOR_BORDER, 0);
  lv_obj_t *icon_repro = lv_label_create(btn_repro);
  lv_label_set_text(icon_repro, LV_SYMBOL_SHUFFLE);
  lv_obj_set_style_text_font(icon_repro, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(icon_repro, lv_color_hex(0xFFAB00), 0);
  lv_obj_center(icon_repro);
  lv_obj_add_event_cb(btn_repro, nav_breeding_cb, LV_EVENT_CLICKED, NULL);

  // === CENTER ===

  // Home button - center, larger
  lv_obj_t *btn_home = lv_btn_create(navbar);
  lv_obj_set_size(btn_home, 65, 48);
  lv_obj_set_style_bg_color(btn_home, COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_color(btn_home, COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_home, 24, 0);
  lv_obj_set_style_shadow_width(btn_home, 12, 0);
  lv_obj_set_style_shadow_color(btn_home, COLOR_PRIMARY, 0);
  lv_obj_set_style_shadow_opa(btn_home, LV_OPA_40, 0);
  lv_obj_set_style_border_width(btn_home, 0, 0);
  lv_obj_t *icon_home = lv_label_create(btn_home);
  lv_label_set_text(icon_home, LV_SYMBOL_HOME);
  lv_obj_set_style_text_font(icon_home, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(icon_home, COLOR_BG_DARK, 0);
  lv_obj_center(icon_home);
  lv_obj_add_event_cb(btn_home, nav_home_cb, LV_EVENT_CLICKED, NULL);

  // === RIGHT SIDE ===

  // Photos button
  lv_obj_t *btn_photos = lv_btn_create(navbar);
  lv_obj_set_size(btn_photos, 50, 44);
  lv_obj_set_style_bg_color(btn_photos, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(btn_photos, lv_color_hex(0x40C4FF),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_photos, 12, 0);
  lv_obj_set_style_border_width(btn_photos, 1, 0);
  lv_obj_set_style_border_color(btn_photos, COLOR_BORDER, 0);
  lv_obj_t *icon_photos = lv_label_create(btn_photos);
  lv_label_set_text(icon_photos, LV_SYMBOL_IMAGE);
  lv_obj_set_style_text_font(icon_photos, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(icon_photos, lv_color_hex(0x40C4FF), 0);
  lv_obj_center(icon_photos);
  lv_obj_add_event_cb(btn_photos, nav_gallery_cb, LV_EVENT_CLICKED, NULL);

  // Export button
  lv_obj_t *btn_export = lv_btn_create(navbar);
  lv_obj_set_size(btn_export, 50, 44);
  lv_obj_set_style_bg_color(btn_export, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(btn_export, lv_color_hex(0x9C27B0),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_export, 12, 0);
  lv_obj_set_style_border_width(btn_export, 1, 0);
  lv_obj_set_style_border_color(btn_export, COLOR_BORDER, 0);
  lv_obj_t *icon_export = lv_label_create(btn_export);
  lv_label_set_text(icon_export, LV_SYMBOL_UPLOAD);
  lv_obj_set_style_text_font(icon_export, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(icon_export, lv_color_hex(0x9C27B0), 0);
  lv_obj_center(icon_export);
  lv_obj_add_event_cb(btn_export, nav_conformity_cb, LV_EVENT_CLICKED, NULL);

  // Settings button (same color style as others)
  lv_obj_t *btn_settings = lv_btn_create(navbar);
  lv_obj_set_size(btn_settings, 50, 44);
  lv_obj_set_style_bg_color(btn_settings, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x607D8B),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_settings, 12, 0);
  lv_obj_set_style_border_width(btn_settings, 1, 0);
  lv_obj_set_style_border_color(btn_settings, COLOR_BORDER, 0);
  lv_obj_t *icon_settings = lv_label_create(btn_settings);
  lv_label_set_text(icon_settings, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_font(icon_settings, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(icon_settings, lv_color_hex(0x607D8B), 0);
  lv_obj_center(icon_settings);
  lv_obj_add_event_cb(btn_settings, nav_settings_cb, LV_EVENT_CLICKED, NULL);
}

// ====================================================================================
// CREATE PAGES
// ====================================================================================

// Forward declarations for callbacks
static void nav_gallery_cb(lv_event_t *e);
static void nav_animals_cb(lv_event_t *e);
static void nav_breeding_cb(lv_event_t *e);
static void nav_conformity_cb(lv_event_t *e);

// Forward declarations for reptile manager functions
static void reptile_count_by_species(int *snakes, int *lizards, int *turtles);
static int reptile_count_feeding_alerts(void);
static void animal_list_item_cb(lv_event_t *e);
static const char *reptile_get_icon(reptile_species_t species);
static int reptile_days_since_feeding(uint8_t animal_id);

static void create_home_page(lv_obj_t *parent) {
  page_home = lv_obj_create(parent);
  lv_obj_set_size(page_home, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_home, 0, 50);
  lv_obj_set_style_bg_color(page_home, COLOR_BG_DARK, 0);
  lv_obj_set_style_bg_opa(page_home, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(page_home, 0, 0);
  lv_obj_set_style_radius(page_home, 0, 0);
  lv_obj_set_style_pad_all(page_home, 10, 0);
  lv_obj_set_flex_flow(page_home, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_home, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(page_home, 8, 0);

  // Title row
  lv_obj_t *title_row = lv_obj_create(page_home);
  lv_obj_set_size(title_row, LCD_H_RES - 20, 35);
  lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(title_row, 0, 0);
  lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(title_row);
  lv_label_set_text_fmt(title, LV_SYMBOL_HOME " Mes Terrariums (%d)",
                        reptile_count);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00D9FF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

  // Animals grid container
  lv_obj_t *terra_grid = lv_obj_create(page_home);
  lv_obj_set_size(terra_grid, LCD_H_RES - 20, LCD_V_RES - 180);
  lv_obj_set_style_bg_opa(terra_grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(terra_grid, 0, 0);
  lv_obj_set_style_pad_all(terra_grid, 5, 0);
  lv_obj_set_flex_flow(terra_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(terra_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(terra_grid, 10, 0);

  // Create a card for each animal
  for (int i = 0; i < reptile_count && i < 6; i++) {
    if (!reptiles[i].active)
      continue;

    lv_color_t border_color =
        (reptiles[i].species == SPECIES_SNAKE)    ? COLOR_SNAKE
        : (reptiles[i].species == SPECIES_LIZARD) ? COLOR_LIZARD
                                                  : COLOR_TURTLE;

    lv_obj_t *card = lv_obj_create(terra_grid);
    lv_obj_set_size(card, 310, 120);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A2940), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A3950), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(card, border_color, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, animal_list_item_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, reptile_get_icon(reptiles[i].species));
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, border_color, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *name_lbl = lv_label_create(card);
    lv_label_set_text(name_lbl, reptiles[i].name);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 30, 2);

    lv_obj_t *species_lbl = lv_label_create(card);
    lv_label_set_text(species_lbl, reptiles[i].species_common);
    lv_obj_set_style_text_color(species_lbl, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(species_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(species_lbl, LV_ALIGN_TOP_LEFT, 30, 20);

    int days = reptile_days_since_feeding(i);
    lv_obj_t *feed_lbl = lv_label_create(card);
    lv_label_set_text_fmt(feed_lbl, "Repas: %dj", days >= 0 ? days : 0);
    lv_obj_set_style_text_font(feed_lbl, &lv_font_montserrat_10, 0);
    int threshold = (reptiles[i].species == SPECIES_SNAKE) ? 7 : 3;
    lv_obj_set_style_text_color(
        feed_lbl,
        days >= threshold ? lv_color_hex(0xF44336) : lv_color_hex(0x4CAF50), 0);
    lv_obj_align(feed_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *settings_btn = lv_btn_create(card);
    lv_obj_set_size(settings_btn, 28, 28);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_RIGHT, 0, -5);
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(0x00B8D4),
                              0); // Cyan color to indicate terrarium settings
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(0x0097A7),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(settings_btn, 6, 0);
    lv_obj_t *set_ico = lv_label_create(settings_btn);
    lv_label_set_text(set_ico, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(set_ico, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(set_ico);
    // Use terrarium_settings_cb to open terrarium-specific settings page
    lv_obj_add_event_cb(settings_btn, terrarium_settings_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
  }

  if (reptile_count == 0) {
    lv_obj_t *empty_lbl = lv_label_create(terra_grid);
    lv_label_set_text(empty_lbl, "Aucun animal\n\nAjoutez via Animaux");
    lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(empty_lbl);
  }

  int feeding_alerts = reptile_count_feeding_alerts();
  if (dashboard_alerts_label) {
    if (feeding_alerts > 0) {
      lv_label_set_text_fmt(dashboard_alerts_label, "%d alertes",
                            feeding_alerts);
    } else {
      lv_label_set_text(dashboard_alerts_label, "0 alertes");
    }
  }
}

static void create_settings_page(lv_obj_t *parent) {
  page_settings = lv_obj_create(parent);
  lv_obj_set_size(page_settings, LCD_H_RES, LCD_V_RES - 120);
  lv_obj_align(page_settings, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_color(page_settings, COLOR_BG_DARK, 0);
  lv_obj_set_style_border_width(page_settings, 0, 0);
  lv_obj_set_style_radius(page_settings, 0, 0);
  lv_obj_set_style_pad_all(page_settings, 16, 0);
  lv_obj_set_flex_flow(page_settings, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_settings, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(page_settings, 10, 0);
  // Note: No HIDDEN flag - navigate_to handles visibility

  // Connectivity Card
  lv_obj_t *conn_card = create_card(page_settings, LCD_H_RES - 32, 130);
  lv_obj_t *conn_title = lv_label_create(conn_card);
  lv_label_set_text(conn_title, LV_SYMBOL_WIFI " Connectivity");
  lv_obj_set_style_text_color(conn_title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(conn_title, &lv_font_montserrat_14, 0);
  lv_obj_align(conn_title, LV_ALIGN_TOP_LEFT, 0, 0);

  // WiFi Row
  lv_obj_t *wifi_row = lv_obj_create(conn_card);
  lv_obj_set_size(wifi_row, LCD_H_RES - 80, 32);
  lv_obj_align(wifi_row, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_obj_set_style_bg_opa(wifi_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wifi_row, 0, 0);
  lv_obj_set_style_pad_all(wifi_row, 0, 0);
  lv_obj_clear_flag(wifi_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *wifi_lbl = lv_label_create(wifi_row);
  lv_label_set_text(wifi_lbl, LV_SYMBOL_WIFI "  WiFi (ESP32-C6)");
  lv_obj_set_style_text_color(wifi_lbl, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(wifi_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  // WiFi Settings button
  lv_obj_t *wifi_settings_btn = lv_button_create(wifi_row);
  lv_obj_set_size(wifi_settings_btn, 80, 28);
  lv_obj_align(wifi_settings_btn, LV_ALIGN_RIGHT_MID, -60, 0);
  lv_obj_set_style_bg_color(wifi_settings_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_radius(wifi_settings_btn, 6, 0);
  lv_obj_add_event_cb(wifi_settings_btn, nav_wifi_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *wifi_btn_lbl = lv_label_create(wifi_settings_btn);
  lv_label_set_text(wifi_btn_lbl, LV_SYMBOL_SETTINGS);
  lv_obj_center(wifi_btn_lbl);

  lv_obj_t *wifi_sw = lv_switch_create(wifi_row);
  lv_obj_align(wifi_sw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(wifi_sw, COLOR_ACCENT, LV_PART_MAIN);
  lv_obj_set_style_bg_color(wifi_sw, COLOR_SUCCESS,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (wifi_enabled)
    lv_obj_add_state(wifi_sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(wifi_sw, wifi_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Bluetooth Row
  lv_obj_t *bt_row = lv_obj_create(conn_card);
  lv_obj_set_size(bt_row, LCD_H_RES - 80, 32);
  lv_obj_align(bt_row, LV_ALIGN_TOP_LEFT, 0, 68);
  lv_obj_set_style_bg_opa(bt_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bt_row, 0, 0);
  lv_obj_set_style_pad_all(bt_row, 0, 0);
  lv_obj_clear_flag(bt_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bt_lbl = lv_label_create(bt_row);
  lv_label_set_text(bt_lbl, LV_SYMBOL_BLUETOOTH "  Bluetooth");
  lv_obj_set_style_text_color(bt_lbl, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(bt_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(bt_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  // Bluetooth Settings button
  lv_obj_t *bt_settings_btn = lv_button_create(bt_row);
  lv_obj_set_size(bt_settings_btn, 80, 28);
  lv_obj_align(bt_settings_btn, LV_ALIGN_RIGHT_MID, -60, 0);
  lv_obj_set_style_bg_color(bt_settings_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_radius(bt_settings_btn, 6, 0);
  lv_obj_add_event_cb(bt_settings_btn, nav_bluetooth_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *bt_btn_lbl = lv_label_create(bt_settings_btn);
  lv_label_set_text(bt_btn_lbl, LV_SYMBOL_SETTINGS);
  lv_obj_center(bt_btn_lbl);

  lv_obj_t *bt_sw = lv_switch_create(bt_row);
  lv_obj_align(bt_sw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(bt_sw, COLOR_ACCENT, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bt_sw, COLOR_PRIMARY,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (bluetooth_enabled)
    lv_obj_add_state(bt_sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(bt_sw, bluetooth_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Display Card
  lv_obj_t *disp_card = create_card(page_settings, LCD_H_RES - 32, 90);
  lv_obj_t *disp_title = lv_label_create(disp_card);
  lv_label_set_text(disp_title, LV_SYMBOL_IMAGE " Display");
  lv_obj_set_style_text_color(disp_title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(disp_title, &lv_font_montserrat_14, 0);
  lv_obj_align(disp_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *disp_info = lv_label_create(disp_card);
  lv_label_set_text_fmt(disp_info, "Resolution: 480 x 800  |  Brightness: %d%%",
                        current_brightness);
  lv_obj_set_style_text_color(disp_info, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(disp_info, &lv_font_montserrat_12, 0);
  lv_obj_align(disp_info, LV_ALIGN_TOP_LEFT, 0, 30);

  // Storage Card
  lv_obj_t *storage_card = create_card(page_settings, LCD_H_RES - 32, 90);
  lv_obj_t *storage_title = lv_label_create(storage_card);
  lv_label_set_text(storage_title, LV_SYMBOL_SD_CARD " Storage");
  lv_obj_set_style_text_color(storage_title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(storage_title, &lv_font_montserrat_14, 0);
  lv_obj_align(storage_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *storage_info = lv_label_create(storage_card);
  if (sd_mounted && sd_card) {
    lv_label_set_text_fmt(storage_info, "SD Card: %s\nCapacity: %llu MB",
                          sd_card->cid.name,
                          (uint64_t)sd_card->csd.capacity *
                              sd_card->csd.sector_size / (1024 * 1024));
  } else {
    lv_label_set_text(storage_info, "SD Card: Not mounted");
  }
  lv_obj_set_style_text_color(storage_info, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(storage_info, &lv_font_montserrat_12, 0);
  lv_obj_align(storage_info, LV_ALIGN_TOP_LEFT, 0, 30);

  // About Card
  lv_obj_t *about_card = create_card(page_settings, LCD_H_RES - 32, 150);
  lv_obj_t *about_title = lv_label_create(about_card);
  lv_label_set_text(about_title, LV_SYMBOL_FILE " About");
  lv_obj_set_style_text_color(about_title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(about_title, &lv_font_montserrat_14, 0);
  lv_obj_align(about_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *about_text = lv_label_create(about_card);
  lv_label_set_text(about_text, "Smart Panel Demo v1.0\n\n"
                                "ESP-IDF:  v6.1-dev\n"
                                "LVGL:     v9.4\n"
                                "ESP-Hosted: v2.8.5\n"
                                "© 2026 IoT Development");
  lv_obj_set_style_text_color(about_text, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(about_text, &lv_font_montserrat_12, 0);
  lv_obj_align(about_text, LV_ALIGN_TOP_LEFT, 0, 28);

  // Firmware Update Card - REMOVED (C6 update already done to v2.8.5)
}

// ====================================================================================
// CREATE WIFI PAGE
// ====================================================================================

static void create_wifi_page(lv_obj_t *parent) {
  page_wifi = lv_obj_create(parent);
  lv_obj_set_size(page_wifi, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_wifi, 0, 50);
  lv_obj_set_style_bg_color(page_wifi, COLOR_BG_DARK, 0);
  lv_obj_set_style_border_width(page_wifi, 0, 0);
  lv_obj_set_style_pad_all(page_wifi, 10, 0);
  // Note: No HIDDEN flag - navigate_to handles visibility
  lv_obj_set_flex_flow(page_wifi, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_wifi, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(page_wifi, 8, 0);

  // Header with back button
  lv_obj_t *header = lv_obj_create(page_wifi);
  lv_obj_set_size(header, LCD_H_RES - 20, 50);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_button_create(header);
  lv_obj_set_size(back_btn, 50, 40);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_ACCENT, 0);
  lv_obj_add_event_cb(back_btn, wifi_back_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
  lv_obj_center(back_lbl);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "WiFi Configuration");
  lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *scan_btn = lv_button_create(header);
  lv_obj_set_size(scan_btn, 80, 40);
  lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(scan_btn, COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *scan_lbl = lv_label_create(scan_btn);
  lv_label_set_text(scan_lbl, "Scan");
  lv_obj_center(scan_lbl);

  // Current Network Card (shown when connected)
  lv_obj_t *current_net_card = lv_obj_create(page_wifi);
  lv_obj_set_size(current_net_card, LCD_H_RES - 20, 100);
  lv_obj_set_style_bg_color(current_net_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_border_color(current_net_card, COLOR_SUCCESS, 0);
  lv_obj_set_style_border_width(current_net_card, 2, 0);
  lv_obj_set_style_radius(current_net_card, 12, 0);
  lv_obj_set_style_pad_all(current_net_card, 10, 0);
  lv_obj_clear_flag(current_net_card, LV_OBJ_FLAG_SCROLLABLE);

  // Card Title - "Reseau actuel"
  lv_obj_t *net_title = lv_label_create(current_net_card);
  lv_label_set_text(net_title, LV_SYMBOL_WIFI " Reseau actuel");
  lv_obj_set_style_text_color(net_title, COLOR_SUCCESS, 0);
  lv_obj_set_style_text_font(net_title, &lv_font_montserrat_14, 0);
  lv_obj_align(net_title, LV_ALIGN_TOP_LEFT, 0, 0);

  // Network name and IP
  lv_obj_t *net_info = lv_label_create(current_net_card);
  if (wifi_connected && strlen(wifi_selected_ssid) > 0) {
    char info_buf[96];
    snprintf(info_buf, sizeof(info_buf), "%s\nIP: %s", wifi_selected_ssid,
             wifi_ip);
    lv_label_set_text(net_info, info_buf);
  } else {
    lv_label_set_text(net_info, "Non connecte");
  }
  lv_obj_set_style_text_color(net_info, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(net_info, &lv_font_montserrat_12, 0);
  lv_obj_align(net_info, LV_ALIGN_TOP_LEFT, 0, 22);

  // Action buttons row
  lv_obj_t *disconnect_btn = lv_button_create(current_net_card);
  lv_obj_set_size(disconnect_btn, 100, 30);
  lv_obj_align(disconnect_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(disconnect_btn, lv_color_hex(0xFF9800), 0);
  lv_obj_set_style_radius(disconnect_btn, 6, 0);
  lv_obj_add_event_cb(disconnect_btn, wifi_disconnect_btn_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *disc_lbl = lv_label_create(disconnect_btn);
  lv_label_set_text(disc_lbl, "Deconnecter");
  lv_obj_set_style_text_font(disc_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(disc_lbl);

  lv_obj_t *forget_btn2 = lv_button_create(current_net_card);
  lv_obj_set_size(forget_btn2, 80, 30);
  lv_obj_align(forget_btn2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(forget_btn2, COLOR_DANGER, 0);
  lv_obj_set_style_radius(forget_btn2, 6, 0);
  lv_obj_add_event_cb(forget_btn2, wifi_forget_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *forg_lbl = lv_label_create(forget_btn2);
  lv_label_set_text(forg_lbl, "Oublier");
  lv_obj_set_style_text_font(forg_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(forg_lbl);

  // Hide card if not connected
  if (!wifi_connected) {
    lv_obj_add_flag(current_net_card, LV_OBJ_FLAG_HIDDEN);
  }

  // Status label
  wifi_status_label = lv_label_create(page_wifi);
  lv_label_set_text(wifi_status_label,
                    wifi_connected ? "Connecte - Scannez pour d'autres reseaux"
                                   : "Scannez pour trouver des reseaux");
  lv_obj_set_style_text_color(wifi_status_label, COLOR_TEXT_DIM, 0);

  // Selected SSID label
  wifi_ssid_label = lv_label_create(page_wifi);
  lv_label_set_text(wifi_ssid_label, "Reseau: (aucun selectionne)");
  lv_obj_set_style_text_color(wifi_ssid_label, COLOR_SUCCESS, 0);

  // WiFi network container (using simple obj instead of lv_list)
  wifi_list = lv_obj_create(page_wifi);
  lv_obj_set_size(wifi_list, LCD_H_RES - 40, 180);
  lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_border_color(wifi_list, lv_color_hex(0x00D9FF), 0);
  lv_obj_set_style_border_width(wifi_list, 2, 0);
  lv_obj_set_style_radius(wifi_list, 10, 0);
  lv_obj_set_style_pad_all(wifi_list, 8, 0);
  lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(wifi_list, 5, 0);
  // Enable scrolling
  lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);

  // Password container (textarea + eye button)
  lv_obj_t *pwd_container = lv_obj_create(page_wifi);
  lv_obj_set_size(pwd_container, LCD_H_RES - 20, 50);
  lv_obj_set_style_bg_opa(pwd_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pwd_container, 0, 0);
  lv_obj_set_style_pad_all(pwd_container, 0, 0);
  lv_obj_clear_flag(pwd_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(pwd_container, LV_OBJ_FLAG_HIDDEN);
  wifi_pwd_container = pwd_container; // Store reference for callbacks

  // Password text area
  wifi_password_ta = lv_textarea_create(pwd_container);
  lv_obj_set_size(wifi_password_ta, LCD_H_RES - 80, 45);
  lv_obj_align(wifi_password_ta, LV_ALIGN_LEFT_MID, 0, 0);
  lv_textarea_set_placeholder_text(wifi_password_ta, "Password...");
  lv_textarea_set_password_mode(wifi_password_ta, true);
  lv_textarea_set_one_line(wifi_password_ta, true);
  lv_obj_set_style_bg_color(wifi_password_ta, COLOR_BG_CARD, 0);
  lv_obj_set_style_text_color(wifi_password_ta, COLOR_TEXT, 0);
  lv_obj_set_style_border_color(wifi_password_ta, COLOR_BORDER, 0);
  lv_obj_set_style_radius(wifi_password_ta, 8, 0);

  // Eye button to toggle password visibility
  lv_obj_t *eye_btn = lv_button_create(pwd_container);
  lv_obj_set_size(eye_btn, 50, 45);
  lv_obj_align(eye_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(eye_btn, COLOR_ACCENT, 0);
  lv_obj_set_style_radius(eye_btn, 8, 0);
  lv_obj_add_event_cb(eye_btn, wifi_password_toggle_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *eye_lbl = lv_label_create(eye_btn);
  lv_label_set_text(eye_lbl, LV_SYMBOL_EYE_CLOSE);
  lv_obj_set_style_text_font(eye_lbl, &lv_font_montserrat_18, 0);
  lv_obj_center(eye_lbl);

  // Connect button
  lv_obj_t *connect_btn = lv_button_create(page_wifi);
  lv_obj_set_size(connect_btn, 200, 45);
  lv_obj_set_style_bg_color(connect_btn, COLOR_SUCCESS, 0);
  lv_obj_set_style_radius(connect_btn, 8, 0);
  lv_obj_add_event_cb(connect_btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *connect_lbl = lv_label_create(connect_btn);
  lv_label_set_text(connect_lbl, LV_SYMBOL_WIFI " Connecter");
  lv_obj_center(connect_lbl);

  // AZERTY Keyboard - BIGGER for easier use
  wifi_keyboard = lv_keyboard_create(page_wifi);
  lv_obj_set_size(wifi_keyboard, LCD_H_RES, 320);
  lv_keyboard_set_textarea(wifi_keyboard, wifi_password_ta);

  // Set custom AZERTY maps for each mode
  lv_keyboard_set_map(wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                      kb_map_azerty_lower, kb_ctrl_lower);
  lv_keyboard_set_map(wifi_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                      kb_map_azerty_upper, kb_ctrl_upper);
  lv_keyboard_set_map(wifi_keyboard, LV_KEYBOARD_MODE_SPECIAL, kb_map_special,
                      kb_ctrl_special);

  // Start in lowercase mode
  lv_keyboard_set_mode(wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

  // Style the keyboard
  lv_obj_set_style_bg_color(wifi_keyboard, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(wifi_keyboard, COLOR_ACCENT, LV_PART_ITEMS);
  lv_obj_set_style_text_color(wifi_keyboard, COLOR_TEXT, LV_PART_ITEMS);

  // Event handler for keyboard:
  // - LV_EVENT_READY: Triggered when OK button is pressed (handles WiFi
  // connection)
  // - Mode switching (ABC/abc/1#): Handled AUTOMATICALLY by LVGL's default
  // handler NOTE: Do NOT add LV_EVENT_VALUE_CHANGED or LV_EVENT_CLICKED
  // handlers for keys! LVGL's default keyboard handler processes key presses
  // and mode switches.
  lv_obj_add_event_cb(wifi_keyboard, wifi_keyboard_ready_cb, LV_EVENT_READY,
                      NULL);
  lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// ====================================================================================
// CREATE BLUETOOTH PAGE
// ====================================================================================

static void create_bluetooth_page(lv_obj_t *parent) {
  page_bluetooth = lv_obj_create(parent);
  lv_obj_set_size(page_bluetooth, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_bluetooth, 0, 50);
  lv_obj_set_style_bg_color(page_bluetooth, COLOR_BG_DARK, 0);
  lv_obj_set_style_border_width(page_bluetooth, 0, 0);
  lv_obj_set_style_pad_all(page_bluetooth, 10, 0);
  // Note: No HIDDEN flag - navigate_to handles visibility
  lv_obj_set_flex_flow(page_bluetooth, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_bluetooth, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(page_bluetooth, 8, 0);

  // Header with back button
  lv_obj_t *header = lv_obj_create(page_bluetooth);
  lv_obj_set_size(header, LCD_H_RES - 20, 50);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_button_create(header);
  lv_obj_set_size(back_btn, 50, 40);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_ACCENT, 0);
  lv_obj_add_event_cb(back_btn, bt_back_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
  lv_obj_center(back_lbl);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, LV_SYMBOL_BLUETOOTH " Bluetooth");
  lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *scan_btn = lv_button_create(header);
  lv_obj_set_size(scan_btn, 80, 40);
  lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(scan_btn, COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(scan_btn, bt_scan_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *scan_lbl = lv_label_create(scan_btn);
  lv_label_set_text(scan_lbl, "Rechercher");
  lv_obj_center(scan_lbl);

  // Status label
  bt_status_label = lv_label_create(page_bluetooth);
#if CONFIG_BT_ENABLED
  lv_label_set_text(bt_status_label,
                    "Appuyez sur 'Rechercher' pour trouver des appareils");
#else
  lv_label_set_text(bt_status_label,
                    "Bluetooth desactive dans la configuration");
#endif
  lv_obj_set_style_text_color(bt_status_label, COLOR_TEXT_DIM, 0);

  // Selected device info label
  bt_device_label = lv_label_create(page_bluetooth);
  lv_label_set_text(bt_device_label, "Appareil: (aucun selectionne)");
  lv_obj_set_style_text_color(bt_device_label, COLOR_PRIMARY, 0);
  lv_obj_set_style_text_font(bt_device_label, &lv_font_montserrat_14, 0);

  // BLE device container (using simple obj instead of lv_list)
  bt_list = lv_obj_create(page_bluetooth);
  lv_obj_set_size(bt_list, LCD_H_RES - 40, 300);
  lv_obj_set_style_bg_color(bt_list, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_border_color(bt_list, lv_color_hex(0x9C27B0), 0); // Violet
  lv_obj_set_style_border_width(bt_list, 2, 0);
  lv_obj_set_style_radius(bt_list, 10, 0);
  lv_obj_set_style_pad_all(bt_list, 8, 0);
  lv_obj_set_flex_flow(bt_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bt_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(bt_list, 5, 0);
  lv_obj_add_flag(bt_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(bt_list, LV_DIR_VER);

  // Info text at bottom - explain BLE limitation
  lv_obj_t *info_label = lv_label_create(page_bluetooth);
  lv_label_set_text(info_label, LV_SYMBOL_WARNING
                    " Mode BLE uniquement\n"
                    "Telephones/PC (Bluetooth Classic) non visibles.\n"
                    "Visible: montres, capteurs, ecouteurs...");
  lv_obj_set_style_text_color(info_label, lv_color_hex(0xFF9800), 0); // Orange
  lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
}

// ====================================================================================
// GALLERY PAGE FUNCTIONS
// ====================================================================================

static void gallery_scan_images(void) {
  gallery_file_count = 0;

  if (!sd_mounted) {
    ESP_LOGW(TAG, "SD Card not mounted, cannot scan for images");
    return;
  }

  DIR *dir = opendir(SD_MOUNT_POINT "/imgs");
  if (!dir) {
    ESP_LOGW(TAG, "Cannot open /sdcard/imgs directory");
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && gallery_file_count < 20) {
    // Check for image extensions
    char *ext = strrchr(entry->d_name, '.');
    if (ext &&
        (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".jpg") == 0 ||
         strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".bmp") == 0)) {
      snprintf(gallery_files[gallery_file_count], sizeof(gallery_files[0]),
               "%s", entry->d_name);
      gallery_file_count++;
    }
  }
  closedir(dir);

  ESP_LOGI(TAG, "Found %d images in /sdcard/imgs", gallery_file_count);
}

static void gallery_update_display(void) {
  if (!gallery_image || !gallery_filename_label || !gallery_index_label)
    return;

  if (gallery_file_count == 0) {
    lv_label_set_text(gallery_filename_label, "Aucune image trouvée");
    lv_label_set_text(gallery_index_label, "0/0");
    lv_image_set_src(gallery_image, NULL);
    return;
  }

  // Update filename label
  lv_label_set_text(gallery_filename_label,
                    gallery_files[gallery_current_index]);

  // Update index label
  char idx_str[16];
  snprintf(idx_str, sizeof(idx_str), "%d/%d", gallery_current_index + 1,
           gallery_file_count);
  lv_label_set_text(gallery_index_label, idx_str);

  // Load image from SD card
  char path[128];
  snprintf(path, sizeof(path), "S:" SD_MOUNT_POINT "/imgs/%s",
           gallery_files[gallery_current_index]);
  lv_image_set_src(gallery_image, path);

  ESP_LOGI(TAG, "Loading image: %s", path);
}

static void gallery_prev_cb(lv_event_t *e) {
  (void)e;
  if (gallery_file_count > 0) {
    gallery_current_index =
        (gallery_current_index - 1 + gallery_file_count) % gallery_file_count;
    if (lvgl_port_lock(10)) {
      gallery_update_display();
      lvgl_port_unlock();
    }
  }
}

static void gallery_next_cb(lv_event_t *e) {
  (void)e;
  if (gallery_file_count > 0) {
    gallery_current_index = (gallery_current_index + 1) % gallery_file_count;
    if (lvgl_port_lock(10)) {
      gallery_update_display();
      lvgl_port_unlock();
    }
  }
}

static void gallery_back_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_HOME);
}

static void gallery_refresh_cb(lv_event_t *e) {
  (void)e;
  gallery_scan_images();
  gallery_current_index = 0;
  if (lvgl_port_lock(10)) {
    gallery_update_display();
    lvgl_port_unlock();
  }
}

static void nav_gallery_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_GALLERY);
}

// ====================================================================================
// REPTILE MANAGER FUNCTIONS
// ====================================================================================

// Initialize demo data for testing
static void reptile_init_demo_data(void) {
  // Demo snakes
  reptiles[0] =
      (reptile_t){.id = 0,
                  .name = "Luna",
                  .species_common = "Python Royal",
                  .morph = "Pastel Banana",
                  .species = SPECIES_SNAKE,
                  .sex = SEX_FEMALE,
                  .birth_year = 2021,
                  .birth_month = 3,
                  .birth_day = 15,
                  .weight_grams = 1800,
                  .terrarium_id = 5,
                  .purchase_price = 350,
                  .last_feeding = time(NULL) - (7 * 24 * 3600), // 7 days ago
                  .health = HEALTH_GOOD,
                  .is_breeding = true,
                  .active = true};
  snprintf(reptiles[0].notes, sizeof(reptiles[0].notes),
           "Reproductrice principale");

  reptiles[1] =
      (reptile_t){.id = 1,
                  .name = "Rex",
                  .species_common = "Boa Constrictor",
                  .morph = "Normal",
                  .species = SPECIES_SNAKE,
                  .sex = SEX_MALE,
                  .birth_year = 2019,
                  .birth_month = 8,
                  .birth_day = 22,
                  .weight_grams = 4500,
                  .terrarium_id = 2,
                  .purchase_price = 200,
                  .last_feeding = time(NULL) - (14 * 24 * 3600), // 14 days ago
                  .health = HEALTH_GOOD,
                  .is_breeding = false,
                  .active = true};

  reptiles[2] = (reptile_t){.id = 2,
                            .name = "Scar",
                            .species_common = "Python Royal",
                            .morph = "Spider",
                            .species = SPECIES_SNAKE,
                            .sex = SEX_MALE,
                            .birth_year = 2020,
                            .birth_month = 5,
                            .birth_day = 10,
                            .weight_grams = 1200,
                            .terrarium_id = 6,
                            .purchase_price = 150,
                            .last_feeding = time(NULL) - (10 * 24 * 3600),
                            .health = HEALTH_GOOD,
                            .is_breeding = true,
                            .active = true};

  // Demo lizards
  reptiles[3] = (reptile_t){.id = 3,
                            .name = "Spike",
                            .species_common = "Gecko Léopard",
                            .morph = "Tangerine",
                            .species = SPECIES_LIZARD,
                            .sex = SEX_MALE,
                            .birth_year = 2022,
                            .birth_month = 6,
                            .birth_day = 1,
                            .weight_grams = 85,
                            .terrarium_id = 8,
                            .purchase_price = 80,
                            .last_feeding = time(NULL) - (3 * 24 * 3600),
                            .health = HEALTH_GOOD,
                            .is_breeding = false,
                            .active = true};

  reptiles[4] = (reptile_t){.id = 4,
                            .name = "Draco",
                            .species_common = "Pogona",
                            .morph = "Red Hypo",
                            .species = SPECIES_LIZARD,
                            .sex = SEX_MALE,
                            .birth_year = 2021,
                            .birth_month = 2,
                            .birth_day = 14,
                            .weight_grams = 420,
                            .terrarium_id = 3,
                            .purchase_price = 120,
                            .last_feeding = time(NULL) - (1 * 24 * 3600),
                            .health = HEALTH_GOOD,
                            .is_breeding = false,
                            .active = true};

  // Demo turtle
  reptiles[5] = (reptile_t){.id = 5,
                            .name = "Shelly",
                            .species_common = "Tortue Hermann",
                            .morph = "",
                            .species = SPECIES_TURTLE,
                            .sex = SEX_FEMALE,
                            .birth_year = 2018,
                            .birth_month = 4,
                            .birth_day = 20,
                            .weight_grams = 850,
                            .terrarium_id = 10,
                            .purchase_price = 180,
                            .last_feeding = time(NULL) - (1 * 24 * 3600),
                            .health = HEALTH_GOOD,
                            .is_breeding = false,
                            .active = true};

  reptile_count = 6;

  // Demo breeding
  breedings[0] = (breeding_record_t){
      .id = 0,
      .female_id = 0,
      .male_id = 2, // Luna x Scar
      .pairing_date = time(NULL) - (50 * 24 * 3600),
      .laying_date = time(NULL) + (10 * 24 * 3600), // estimated in 10 days
      .egg_count = 0,
      .hatch_date = 0,
      .hatched_count = 0,
      .active = true};
  breeding_count = 1;

  // Demo inventory
  snprintf(inventory[0].name, sizeof(inventory[0].name), "Souris adultes");
  inventory[0].quantity = 45;
  inventory[0].alert_threshold = 20;
  snprintf(inventory[0].unit, sizeof(inventory[0].unit), "pcs");

  snprintf(inventory[1].name, sizeof(inventory[1].name), "Rats");
  inventory[1].quantity = 12;
  inventory[1].alert_threshold = 5;
  snprintf(inventory[1].unit, sizeof(inventory[1].unit), "pcs");

  snprintf(inventory[2].name, sizeof(inventory[2].name), "Grillons");
  inventory[2].quantity = 200;
  inventory[2].alert_threshold = 50;
  snprintf(inventory[2].unit, sizeof(inventory[2].unit), "pcs");

  inventory_count = 3;

  ESP_LOGI(TAG,
           "Reptile demo data initialized: %d animals, %d breedings, %d "
           "inventory items",
           reptile_count, breeding_count, inventory_count);
}

// Count animals by species
static void reptile_count_by_species(int *snakes, int *lizards, int *turtles) {
  *snakes = 0;
  *lizards = 0;
  *turtles = 0;
  for (int i = 0; i < reptile_count; i++) {
    if (!reptiles[i].active)
      continue;
    switch (reptiles[i].species) {
    case SPECIES_SNAKE:
      (*snakes)++;
      break;
    case SPECIES_LIZARD:
      (*lizards)++;
      break;
    case SPECIES_TURTLE:
      (*turtles)++;
      break;
    default:
      break;
    }
  }
}

// Get days since last feeding
static int reptile_days_since_feeding(uint8_t id) {
  if (id >= reptile_count)
    return -1;
  if (reptiles[id].last_feeding == 0)
    return -1;
  time_t now = time(NULL);
  return (now - reptiles[id].last_feeding) / (24 * 3600);
}

// Get species icon character
static const char *reptile_get_icon(reptile_species_t species) {
  switch (species) {
  case SPECIES_SNAKE:
    return LV_SYMBOL_LOOP; // Snake-like
  case SPECIES_LIZARD:
    return LV_SYMBOL_EYE_OPEN;
  case SPECIES_TURTLE:
    return LV_SYMBOL_HOME;
  default:
    return LV_SYMBOL_DUMMY;
  }
}

// Get sex symbol
static const char *reptile_get_sex_symbol(reptile_sex_t sex) {
  switch (sex) {
  case SEX_MALE:
    return "♂";
  case SEX_FEMALE:
    return "♀";
  default:
    return "?";
  }
}

// Count today's feeding alerts
static int reptile_count_feeding_alerts(void) {
  int count = 0;
  for (int i = 0; i < reptile_count; i++) {
    if (!reptiles[i].active)
      continue;
    int days = reptile_days_since_feeding(i);
    // Snakes: alert after 7-14 days, Lizards: 2-3 days, Turtles: 1-2 days
    int threshold = 7;
    if (reptiles[i].species == SPECIES_LIZARD)
      threshold = 3;
    if (reptiles[i].species == SPECIES_TURTLE)
      threshold = 2;
    if (days >= threshold)
      count++;
  }
  return count;
}

// Navigation callbacks for reptile pages
static void nav_animals_cb(lv_event_t *e);
static void nav_calendar_cb(lv_event_t *e);
static void nav_breeding_cb(lv_event_t *e);
static void animal_list_item_cb(lv_event_t *e);
static void animal_back_cb(lv_event_t *e);
static void animal_detail_back_cb(lv_event_t *e); // Returns to Animals list
static void animal_feed_cb(lv_event_t *e);

// ====================================================================================
// CREATE GALLERY PAGE
// ====================================================================================

static void create_gallery_page(lv_obj_t *parent) {
  page_gallery = lv_obj_create(parent);
  lv_obj_set_size(page_gallery, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_gallery, 0, 50);
  lv_obj_set_style_bg_color(page_gallery, COLOR_BG_DARK, 0);
  lv_obj_set_style_border_width(page_gallery, 0, 0);
  lv_obj_set_style_pad_all(page_gallery, 10, 0);
  // Note: No HIDDEN flag - navigate_to handles visibility
  lv_obj_clear_flag(page_gallery, LV_OBJ_FLAG_SCROLLABLE);

  // Header with back button
  lv_obj_t *header = lv_obj_create(page_gallery);
  lv_obj_set_size(header, LCD_H_RES - 20, 40);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_button_create(header);
  lv_obj_set_size(back_btn, 70, 32);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_ACCENT, 0);
  lv_obj_set_style_radius(back_btn, 6, 0);
  lv_obj_add_event_cb(back_btn, gallery_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_lbl);

  lv_obj_t *title_lbl = lv_label_create(header);
  lv_label_set_text(title_lbl, LV_SYMBOL_IMAGE " Galerie");
  lv_obj_set_style_text_color(title_lbl, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_16, 0);
  lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *refresh_btn = lv_button_create(header);
  lv_obj_set_size(refresh_btn, 40, 32);
  lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(refresh_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_radius(refresh_btn, 6, 0);
  lv_obj_add_event_cb(refresh_btn, gallery_refresh_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *refresh_lbl = lv_label_create(refresh_btn);
  lv_label_set_text(refresh_lbl, LV_SYMBOL_REFRESH);
  lv_obj_center(refresh_lbl);

  // Image display area
  lv_obj_t *img_container = lv_obj_create(page_gallery);
  lv_obj_set_size(img_container, LCD_H_RES - 40, LCD_V_RES - 250);
  lv_obj_align(img_container, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_bg_color(img_container, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(img_container, 12, 0);
  lv_obj_set_style_border_width(img_container, 1, 0);
  lv_obj_set_style_border_color(img_container, COLOR_BORDER, 0);
  lv_obj_clear_flag(img_container, LV_OBJ_FLAG_SCROLLABLE);

  gallery_image = lv_image_create(img_container);
  lv_obj_center(gallery_image);
  lv_image_set_scale(gallery_image, 256); // 1:1 scale
  lv_obj_set_style_radius(gallery_image, 8, 0);
  lv_obj_set_style_clip_corner(gallery_image, true, 0);

  // Navigation controls
  lv_obj_t *nav_container = lv_obj_create(page_gallery);
  lv_obj_set_size(nav_container, LCD_H_RES - 40, 60);
  lv_obj_align(nav_container, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(nav_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(nav_container, 0, 0);
  lv_obj_clear_flag(nav_container, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *prev_btn = lv_button_create(nav_container);
  lv_obj_set_size(prev_btn, 80, 45);
  lv_obj_align(prev_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(prev_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_radius(prev_btn, 10, 0);
  lv_obj_add_event_cb(prev_btn, gallery_prev_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *prev_lbl = lv_label_create(prev_btn);
  lv_label_set_text(prev_lbl, LV_SYMBOL_LEFT " Préc");
  lv_obj_center(prev_lbl);

  // Filename and index display
  lv_obj_t *info_container = lv_obj_create(nav_container);
  lv_obj_set_size(info_container, 260, 50);
  lv_obj_align(info_container, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(info_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(info_container, 0, 0);
  lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);

  gallery_filename_label = lv_label_create(info_container);
  lv_label_set_text(gallery_filename_label, "Aucune image");
  lv_obj_set_style_text_color(gallery_filename_label, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(gallery_filename_label, &lv_font_montserrat_12, 0);
  lv_obj_set_width(gallery_filename_label, 260);
  lv_label_set_long_mode(gallery_filename_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(gallery_filename_label, LV_ALIGN_TOP_MID, 0, 5);

  gallery_index_label = lv_label_create(info_container);
  lv_label_set_text(gallery_index_label, "0/0");
  lv_obj_set_style_text_color(gallery_index_label, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(gallery_index_label, &lv_font_montserrat_14, 0);
  lv_obj_align(gallery_index_label, LV_ALIGN_BOTTOM_MID, 0, -5);

  lv_obj_t *next_btn = lv_button_create(nav_container);
  lv_obj_set_size(next_btn, 80, 45);
  lv_obj_align(next_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(next_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_radius(next_btn, 10, 0);
  lv_obj_add_event_cb(next_btn, gallery_next_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *next_lbl = lv_label_create(next_btn);
  lv_label_set_text(next_lbl, "Suiv " LV_SYMBOL_RIGHT);
  lv_obj_center(next_lbl);
}

// ====================================================================================
// CREATE ANIMALS LIST PAGE
// ====================================================================================

static void update_animal_list(void) {
  if (!animal_list)
    return;
  lv_obj_clean(animal_list);

  for (int i = 0; i < reptile_count; i++) {
    if (!reptiles[i].active)
      continue;

    // Create button manually (not using lv_list)
    lv_obj_t *btn = lv_btn_create(animal_list);
    lv_obj_set_size(btn, lv_pct(100), 65);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E7D32), 0); // Vert foncé
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x4CAF50), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, animal_list_item_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);

    // Icon based on species
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, reptile_get_icon(reptiles[i].species));
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_color_t icon_color = lv_color_hex(0xFFFFFF);
    if (reptiles[i].species == SPECIES_SNAKE)
      icon_color = COLOR_SNAKE;
    else if (reptiles[i].species == SPECIES_LIZARD)
      icon_color = COLOR_LIZARD;
    else if (reptiles[i].species == SPECIES_TURTLE)
      icon_color = COLOR_TURTLE;
    lv_obj_set_style_text_color(icon, icon_color, 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 8, 0);

    // Name and species
    lv_obj_t *name_lbl = lv_label_create(btn);
    lv_label_set_text_fmt(name_lbl, "%s (%s)", reptiles[i].name,
                          reptiles[i].species_common);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 45, 8);

    // Details line (sex, age, last feeding)
    int days = reptile_days_since_feeding(i);
    lv_obj_t *detail_lbl = lv_label_create(btn);
    lv_label_set_text_fmt(detail_lbl, "%s  |  Terra #%d  |  Dernier repas: %dj",
                          reptile_get_sex_symbol(reptiles[i].sex),
                          reptiles[i].terrarium_id, days >= 0 ? days : 0);
    lv_obj_set_style_text_color(detail_lbl, lv_color_hex(0xB0BEC5), 0);
    lv_obj_set_style_text_font(detail_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(detail_lbl, LV_ALIGN_BOTTOM_LEFT, 45, -8);

    // Feeding alert indicator
    int threshold = (reptiles[i].species == SPECIES_SNAKE)    ? 7
                    : (reptiles[i].species == SPECIES_LIZARD) ? 3
                                                              : 2;
    if (days >= threshold) {
      lv_obj_t *alert = lv_label_create(btn);
      lv_label_set_text(alert, LV_SYMBOL_WARNING);
      lv_obj_set_style_text_color(alert, lv_color_hex(0xFFEB3B), 0);
      lv_obj_align(alert, LV_ALIGN_RIGHT_MID, -10, 0);
    }

    ESP_LOGI(TAG, "  Added animal: %s", reptiles[i].name);
  }

  lv_obj_invalidate(animal_list);
}

static void create_animals_page(lv_obj_t *parent) {
  page_animals = lv_obj_create(parent);
  lv_obj_set_size(page_animals, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_animals, 0, 50); // Normal position
  lv_obj_set_style_bg_color(page_animals, COLOR_BG_DARK, 0);
  lv_obj_set_style_bg_opa(page_animals, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(page_animals, 0, 0);
  lv_obj_set_style_pad_all(page_animals, 10, 0);
  lv_obj_set_flex_flow(page_animals, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_animals, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // Header
  lv_obj_t *header = lv_obj_create(page_animals);
  lv_obj_set_size(header, LCD_H_RES - 20, 40);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_button_create(header);
  lv_obj_set_size(back_btn, 70, 32);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_ACCENT, 0);
  lv_obj_set_style_radius(back_btn, 6, 0);
  lv_obj_add_event_cb(back_btn, animal_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_lbl);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text_fmt(title, LV_SYMBOL_LIST " Mes Animaux (%d)",
                        reptile_count);
  lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // Animals container (using simple obj instead of lv_list)
  animal_list = lv_obj_create(page_animals);
  lv_obj_set_size(animal_list, LCD_H_RES - 30, LCD_V_RES - 180);
  lv_obj_set_style_bg_color(animal_list, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_border_color(animal_list, lv_color_hex(0x4CAF50), 0);
  lv_obj_set_style_border_width(animal_list, 2, 0);
  lv_obj_set_style_radius(animal_list, 10, 0);
  lv_obj_set_style_pad_all(animal_list, 8, 0);
  lv_obj_set_flex_flow(animal_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(animal_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(animal_list, 6, 0);
  lv_obj_add_flag(animal_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(animal_list, LV_DIR_VER);

  update_animal_list();
}

// ====================================================================================
// CREATE ANIMAL DETAIL PAGE
// ====================================================================================

// Note: detail_name_label and detail_info_label are declared as global
// variables

static void update_animal_detail(void) {
  if (selected_animal_id < 0 || selected_animal_id >= reptile_count)
    return;
  reptile_t *r = &reptiles[selected_animal_id];

  if (detail_name_label) {
    lv_label_set_text_fmt(detail_name_label, "%s %s",
                          reptile_get_icon(r->species), r->name);
  }

  if (detail_info_label) {
    int age_years = 2026 - r->birth_year;
    int days = reptile_days_since_feeding(selected_animal_id);
    lv_label_set_text_fmt(detail_info_label,
                          "Espèce: %s\n"
                          "Morph: %s\n"
                          "Sexe: %s  |  Age: %d ans\n"
                          "Poids: %d g\n"
                          "Terrarium: #%d\n"
                          "Prix d'achat: %d €\n"
                          "Dernier repas: il y a %d jours",
                          r->species_common, r->morph[0] ? r->morph : "-",
                          reptile_get_sex_symbol(r->sex), age_years,
                          r->weight_grams, r->terrarium_id, r->purchase_price,
                          days >= 0 ? days : 0);
  }
}

static void create_animal_detail_page(lv_obj_t *parent) {
  page_animal_detail = lv_obj_create(parent);
  lv_obj_set_size(page_animal_detail, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_animal_detail, 0, 50);
  lv_obj_set_style_bg_color(page_animal_detail, COLOR_BG_DARK, 0);
  lv_obj_set_style_bg_opa(page_animal_detail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(page_animal_detail, 0, 0);
  lv_obj_set_style_pad_all(page_animal_detail, 8, 0);
  lv_obj_set_flex_flow(page_animal_detail, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_animal_detail, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(page_animal_detail, 8, 0);

  // Header with back button and name
  lv_obj_t *header = lv_obj_create(page_animal_detail);
  lv_obj_set_size(header, LCD_H_RES - 16, 45);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  // Back button - round style
  lv_obj_t *back_btn = lv_button_create(header);
  lv_obj_set_size(back_btn, 40, 40);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(back_btn, 20, 0);
  lv_obj_set_style_border_width(back_btn, 1, 0);
  lv_obj_set_style_border_color(back_btn, COLOR_BORDER, 0);
  lv_obj_add_event_cb(back_btn, animal_detail_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_lbl, COLOR_TEXT, 0);
  lv_obj_center(back_lbl);

  // Animal name - larger font
  detail_name_label = lv_label_create(header);
  lv_label_set_text(detail_name_label, "Animal");
  lv_obj_set_style_text_color(detail_name_label, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(detail_name_label, &lv_font_montserrat_20, 0);
  lv_obj_align(detail_name_label, LV_ALIGN_CENTER, 10, 0);

  // Main info card - prettier layout
  lv_obj_t *info_card = create_card(page_animal_detail, LCD_H_RES - 20, 200);
  lv_obj_set_flex_flow(info_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(info_card, 6, 0);

  // Species row with icon
  lv_obj_t *species_row = lv_obj_create(info_card);
  lv_obj_set_size(species_row, LCD_H_RES - 50, 30);
  lv_obj_set_style_bg_opa(species_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(species_row, 0, 0);
  lv_obj_set_style_pad_all(species_row, 0, 0);
  lv_obj_clear_flag(species_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *species_icon = lv_label_create(species_row);
  lv_label_set_text(species_icon, LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(species_icon, COLOR_PRIMARY, 0);
  lv_obj_set_style_text_font(species_icon, &lv_font_montserrat_18, 0);
  lv_obj_align(species_icon, LV_ALIGN_LEFT_MID, 0, 0);

  // Info label with better formatting
  detail_info_label = lv_label_create(info_card);
  lv_label_set_text(detail_info_label, "...");
  lv_obj_set_style_text_color(detail_info_label, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(detail_info_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_line_space(detail_info_label, 6, 0);

  // Feeding status indicator
  lv_obj_t *status_row = lv_obj_create(info_card);
  lv_obj_set_size(status_row, LCD_H_RES - 50, 35);
  lv_obj_set_style_bg_color(status_row, COLOR_ACCENT, 0);
  lv_obj_set_style_bg_opa(status_row, LV_OPA_50, 0);
  lv_obj_set_style_radius(status_row, 8, 0);
  lv_obj_set_style_border_width(status_row, 0, 0);
  lv_obj_set_style_pad_all(status_row, 4, 0);
  lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *status_icon = lv_label_create(status_row);
  lv_label_set_text(status_icon, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(status_icon, COLOR_WARNING, 0);
  lv_obj_set_style_text_font(status_icon, &lv_font_montserrat_16, 0);
  lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 5, 0);

  lv_obj_t *status_text = lv_label_create(status_row);
  lv_label_set_text(status_text, "Dernier repas: ...");
  lv_obj_set_style_text_color(status_text, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(status_text, &lv_font_montserrat_12, 0);
  lv_obj_align(status_text, LV_ALIGN_LEFT_MID, 30, 0);

  // Action buttons row - more stylish
  lv_obj_t *actions = lv_obj_create(page_animal_detail);
  lv_obj_set_size(actions, LCD_H_RES - 20, 55);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Feed button - primary action with glow
  lv_obj_t *feed_btn = lv_button_create(actions);
  lv_obj_set_size(feed_btn, 110, 45);
  lv_obj_set_style_bg_color(feed_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_color(feed_btn, COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
  lv_obj_set_style_radius(feed_btn, 12, 0);
  lv_obj_set_style_shadow_width(feed_btn, 10, 0);
  lv_obj_set_style_shadow_color(feed_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_shadow_opa(feed_btn, LV_OPA_40, 0);
  lv_obj_add_event_cb(feed_btn, animal_feed_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *feed_lbl = lv_label_create(feed_btn);
  lv_label_set_text(feed_lbl, LV_SYMBOL_OK " Nourrir");
  lv_obj_set_style_text_color(feed_lbl, COLOR_BG_DARK, 0);
  lv_obj_set_style_text_font(feed_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(feed_lbl);

  // Weight button
  lv_obj_t *weight_btn = lv_button_create(actions);
  lv_obj_set_size(weight_btn, 90, 45);
  lv_obj_set_style_bg_color(weight_btn, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(weight_btn, COLOR_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(weight_btn, 12, 0);
  lv_obj_set_style_border_width(weight_btn, 1, 0);
  lv_obj_set_style_border_color(weight_btn, COLOR_BORDER, 0);
  lv_obj_t *weight_lbl = lv_label_create(weight_btn);
  lv_label_set_text(weight_lbl, LV_SYMBOL_EDIT " Pesée");
  lv_obj_set_style_text_color(weight_lbl, COLOR_TEXT, 0);
  lv_obj_center(weight_lbl);

  // Health button
  lv_obj_t *health_btn = lv_button_create(actions);
  lv_obj_set_size(health_btn, 90, 45);
  lv_obj_set_style_bg_color(health_btn, COLOR_BG_CARD, 0);
  lv_obj_set_style_bg_color(health_btn, COLOR_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(health_btn, 12, 0);
  lv_obj_set_style_border_width(health_btn, 1, 0);
  lv_obj_set_style_border_color(health_btn, COLOR_BORDER, 0);
  lv_obj_t *health_lbl = lv_label_create(health_btn);
  lv_label_set_text(health_lbl, LV_SYMBOL_PLUS " Santé");
  lv_obj_set_style_text_color(health_lbl, COLOR_TEXT, 0);
  lv_obj_center(health_lbl);
}

// ====================================================================================
// CREATE BREEDING PAGE
// ====================================================================================

static void create_breeding_page(lv_obj_t *parent) {
  page_breeding = lv_obj_create(parent);
  lv_obj_set_size(page_breeding, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_breeding, 0, 50); // Normal position
  lv_obj_set_style_bg_color(page_breeding, COLOR_BG_DARK, 0);
  lv_obj_set_style_bg_opa(page_breeding, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(page_breeding, 0, 0);
  lv_obj_set_style_pad_all(page_breeding, 10, 0);
  lv_obj_set_flex_flow(page_breeding, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_breeding, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(page_breeding, 10, 0);

  // Header
  lv_obj_t *header = lv_obj_create(page_breeding);
  lv_obj_set_size(header, LCD_H_RES - 20, 40);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_button_create(header);
  lv_obj_set_size(back_btn, 70, 32);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_ACCENT, 0);
  lv_obj_add_event_cb(back_btn, animal_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_lbl);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, LV_SYMBOL_SHUFFLE " Reproduction");
  lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // Active breedings card
  lv_obj_t *breed_card = create_card(page_breeding, LCD_H_RES - 30, 120);
  lv_obj_t *breed_title = lv_label_create(breed_card);
  lv_label_set_text(breed_title, "Accouplements en cours");
  lv_obj_set_style_text_color(breed_title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(breed_title, &lv_font_montserrat_14, 0);

  if (breeding_count > 0) {
    lv_obj_t *breed_info = lv_label_create(breed_card);
    // Find female and male names
    char female_name[32] = "?";
    char male_name[32] = "?";
    if (breedings[0].female_id < reptile_count)
      snprintf(female_name, sizeof(female_name), "%s",
               reptiles[breedings[0].female_id].name);
    if (breedings[0].male_id < reptile_count)
      snprintf(male_name, sizeof(male_name), "%s",
               reptiles[breedings[0].male_id].name);

    int days_to_laying = (breedings[0].laying_date - time(NULL)) / (24 * 3600);
    lv_label_set_text_fmt(breed_info,
                          "%s ♀ × %s ♂\n"
                          "Ponte estimée dans: %d jours",
                          female_name, male_name,
                          days_to_laying > 0 ? days_to_laying : 0);
    lv_obj_set_style_text_color(breed_info, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(breed_info, &lv_font_montserrat_12, 0);
    lv_obj_align(breed_info, LV_ALIGN_TOP_LEFT, 0, 30);
  }

  // Incubation info
  lv_obj_t *incub_card = create_card(page_breeding, LCD_H_RES - 30, 80);
  lv_obj_t *incub_title = lv_label_create(incub_card);
  lv_label_set_text(incub_title, LV_SYMBOL_EYE_OPEN " Incubations");
  lv_obj_set_style_text_color(incub_title, COLOR_EGG, 0);
  lv_obj_t *incub_info = lv_label_create(incub_card);
  lv_label_set_text(incub_info, "Aucune incubation en cours");
  lv_obj_set_style_text_color(incub_info, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(incub_info, &lv_font_montserrat_12, 0);
  lv_obj_align(incub_info, LV_ALIGN_TOP_LEFT, 0, 30);
}

// ====================================================================================
// CONFORMITY / EXPORT FUNCTIONS (Wrappers for UI callbacks)
// ====================================================================================

// Wrapper: Export registre using new compliant function
static esp_err_t export_registre_csv_wrapper(void) {
  return export_registre_csv("/sdcard/registre_reptiles.csv");
}

// Wrapper: Generate attestation using new compliant function
static esp_err_t generate_attestation_cession(int animal_id,
                                              const char *buyer_name,
                                              const char *buyer_address) {
  if (animal_id < 0 || animal_id >= reptile_count) {
    return ESP_ERR_INVALID_ARG;
  }

  char filename[64];
  snprintf(filename, sizeof(filename), "/sdcard/cession_%s_%lld.txt",
           reptiles[animal_id].name, (long long)time(NULL));

  return create_attestation_cession((uint8_t)animal_id,
                                    buyer_name ? buyer_name : "",
                                    buyer_address ? buyer_address : "",
                                    0, // Don par défaut
                                    filename);
}

// Callback for export button
static void export_registre_cb(lv_event_t *e) {
  (void)e;
  esp_err_t ret = export_registre_csv_wrapper();
  if (conformity_status_label) {
    if (ret == ESP_OK) {
      lv_label_set_text(conformity_status_label, LV_SYMBOL_OK
                        " Export reussi!\n/sdcard/registre_reptiles.csv");
      lv_obj_set_style_text_color(conformity_status_label, COLOR_SUCCESS, 0);
    } else {
      lv_label_set_text(conformity_status_label, LV_SYMBOL_WARNING
                        " Echec export\nVerifiez la carte SD");
      lv_obj_set_style_text_color(conformity_status_label, COLOR_DANGER, 0);
    }
  }
}

// Callback for attestation button
static void generate_attestation_cb(lv_event_t *e) {
  (void)e;
  // Generate attestation for first animal as demo
  if (reptile_count > 0) {
    esp_err_t ret = generate_attestation_cession(0, NULL, NULL);
    if (conformity_status_label) {
      if (ret == ESP_OK) {
        lv_label_set_text(conformity_status_label,
                          LV_SYMBOL_OK " Attestation creee!\nSur carte SD");
        lv_obj_set_style_text_color(conformity_status_label, COLOR_SUCCESS, 0);
      } else {
        lv_label_set_text(conformity_status_label,
                          LV_SYMBOL_WARNING " Echec creation");
        lv_obj_set_style_text_color(conformity_status_label, COLOR_DANGER, 0);
      }
    }
  }
}

// Navigation callback for conformity
static void nav_conformity_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_CONFORMITY);
}

static void conformity_back_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_HOME);
}

static void create_conformity_page(lv_obj_t *parent) {
  page_conformity = lv_obj_create(parent);
  lv_obj_set_size(page_conformity, LCD_H_RES, LCD_V_RES - 50 - 60);
  lv_obj_set_pos(page_conformity, 0, 50);
  lv_obj_set_style_bg_color(page_conformity, COLOR_BG_DARK, 0);
  lv_obj_set_style_bg_opa(page_conformity, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(page_conformity, 0, 0);
  lv_obj_set_style_pad_all(page_conformity, 10, 0);
  lv_obj_set_flex_flow(page_conformity, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_conformity, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(page_conformity, 10, 0);

  // Header
  lv_obj_t *header = lv_obj_create(page_conformity);
  lv_obj_set_size(header, LCD_H_RES - 20, 40);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_button_create(header);
  lv_obj_set_size(back_btn, 70, 32);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(back_btn, COLOR_ACCENT, 0);
  lv_obj_add_event_cb(back_btn, conformity_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Retour");
  lv_obj_center(back_lbl);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, LV_SYMBOL_LIST " Conformité");
  lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 20, 0);

  // Info card
  lv_obj_t *info_card = create_card(page_conformity, LCD_H_RES - 30, 80);
  lv_obj_t *info_title = lv_label_create(info_card);
  lv_label_set_text(info_title, LV_SYMBOL_FILE " Registre d'élevage");
  lv_obj_set_style_text_color(info_title, COLOR_PRIMARY, 0);
  lv_obj_set_style_text_font(info_title, &lv_font_montserrat_14, 0);

  char info_text[64];
  snprintf(info_text, sizeof(info_text), "%d animaux enregistrés",
           reptile_count);
  lv_obj_t *info_label = lv_label_create(info_card);
  lv_label_set_text(info_label, info_text);
  lv_obj_set_style_text_color(info_label, COLOR_TEXT_DIM, 0);
  lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 0, 30);

  // Export button
  lv_obj_t *export_btn = lv_button_create(page_conformity);
  lv_obj_set_size(export_btn, LCD_H_RES - 40, 50);
  lv_obj_set_style_bg_color(export_btn, COLOR_PRIMARY, 0);
  lv_obj_set_style_radius(export_btn, 8, 0);
  lv_obj_add_event_cb(export_btn, export_registre_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *export_lbl = lv_label_create(export_btn);
  lv_label_set_text(export_lbl, LV_SYMBOL_DOWNLOAD " Exporter Registre CSV");
  lv_obj_set_style_text_font(export_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(export_lbl);

  // Attestation button
  lv_obj_t *attest_btn = lv_button_create(page_conformity);
  lv_obj_set_size(attest_btn, LCD_H_RES - 40, 50);
  lv_obj_set_style_bg_color(attest_btn, COLOR_ACCENT, 0);
  lv_obj_set_style_radius(attest_btn, 8, 0);
  lv_obj_add_event_cb(attest_btn, generate_attestation_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *attest_lbl = lv_label_create(attest_btn);
  lv_label_set_text(attest_lbl, LV_SYMBOL_EDIT " Attestation de Cession");
  lv_obj_set_style_text_font(attest_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(attest_lbl);

  // Status label
  conformity_status_label = lv_label_create(page_conformity);
  lv_label_set_text(conformity_status_label, "Prêt pour export");
  lv_obj_set_style_text_color(conformity_status_label, COLOR_TEXT_DIM, 0);
  lv_obj_set_style_text_font(conformity_status_label, &lv_font_montserrat_12,
                             0);
  lv_obj_set_style_text_align(conformity_status_label, LV_TEXT_ALIGN_CENTER, 0);
}

// ====================================================================================
// REPTILE NAVIGATION CALLBACKS
// ====================================================================================

static void nav_animals_cb(lv_event_t *e) {
  (void)e;
  ESP_LOGI(TAG, "Opening Animals page");
  navigate_to(PAGE_ANIMALS);
}

static void nav_breeding_cb(lv_event_t *e) {
  (void)e;
  ESP_LOGI(TAG, "Opening Breeding page");
  navigate_to(PAGE_BREEDING);
}

static void animal_list_item_cb(lv_event_t *e) {
  selected_animal_id = (int)(intptr_t)lv_event_get_user_data(e);
  ESP_LOGI(TAG, "Selected animal ID: %d", selected_animal_id);
  navigate_to(PAGE_ANIMAL_DETAIL);
}

static void animal_back_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_HOME);
}

static void animal_detail_back_cb(lv_event_t *e) {
  (void)e;
  navigate_to(PAGE_ANIMALS); // Return to Animals list, not Home
}

static void animal_feed_cb(lv_event_t *e) {
  (void)e;
  if (selected_animal_id >= 0 && selected_animal_id < reptile_count) {
    reptiles[selected_animal_id].last_feeding = time(NULL);
    ESP_LOGI(TAG, "Fed animal: %s", reptiles[selected_animal_id].name);
    // No lock needed - already in LVGL event context
    update_animal_detail();
  }
}

static void create_ui(void) {
  if (!lvgl_port_lock(1000))
    return;

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COLOR_BG_DARK, 0);

  // Create fixed elements (status bar and navbar)
  create_status_bar(scr);
  create_navbar(scr);

  // Navigate to home page (creates only that page)
  navigate_to(PAGE_HOME);

  lvgl_port_unlock();
  ESP_LOGI(TAG, "UI created");
}

static void update_status_bar(void) {
  // Fallback counter for when time is not synced
  static uint32_t secs = 0;
  static bool blink_state = false;
  secs++;
  blink_state = !blink_state;

  if (lvgl_port_lock(10)) {
    struct tm current_time;
    bool time_valid = get_current_time(&current_time);

    if (label_time) {
      if (time_valid) {
        // Display real time in 24h format with seconds indicator
        if (blink_state) {
          lv_label_set_text_fmt(label_time, "%02d:%02d", current_time.tm_hour,
                                current_time.tm_min);
        } else {
          lv_label_set_text_fmt(label_time, "%02d %02d", current_time.tm_hour,
                                current_time.tm_min);
        }
        // Green color when synced
        lv_obj_set_style_text_color(label_time, COLOR_SUCCESS, 0);
      } else {
        // Fallback to counter if time not synced (blinking orange)
        lv_label_set_text_fmt(label_time, "%02lu:%02lu", (secs / 60) % 24,
                              secs % 60);
        // Orange/yellow when not synced
        lv_obj_set_style_text_color(
            label_time, blink_state ? COLOR_WARNING : COLOR_TEXT_DIM, 0);
      }
    }

    if (label_date) {
      if (time_valid) {
        // French month names
        const char *months_fr[] = {"Jan", "Fév", "Mar", "Avr", "Mai", "Jun",
                                   "Jul", "Aoû", "Sep", "Oct", "Nov", "Déc"};
        lv_label_set_text_fmt(label_date, "%02d %s", current_time.tm_mday,
                              months_fr[current_time.tm_mon]);
        lv_obj_set_style_text_color(label_date, COLOR_TEXT, 0);
      } else {
        // Show "Synchro..." when not synced
        lv_label_set_text(label_date, blink_state ? "Synchro" : "...");
        lv_obj_set_style_text_color(label_date, COLOR_TEXT_DIM, 0);
      }
    }

    lvgl_port_unlock();
  }
}

// ====================================================================================
// MAIN
// ====================================================================================

void app_main(void) {
  ESP_LOGI(TAG, "=========================================");
  ESP_LOGI(TAG, "  Smart Panel - GUITION JC4880P443C");
  ESP_LOGI(TAG, "  ESP-IDF 6.1 | LVGL 9.4 | SD Card");
  ESP_LOGI(TAG, "=========================================");

  // Init NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Init WiFi (via ESP32-C6)
  wifi_init();

  // Auto-connect to saved WiFi network if credentials exist
  if (wifi_has_saved_credentials()) {
    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};
    if (wifi_load_credentials(saved_ssid, sizeof(saved_ssid), saved_pass,
                              sizeof(saved_pass)) == ESP_OK) {
      ESP_LOGI(TAG, "Auto-connecting to saved WiFi: %s", saved_ssid);
      wifi_start();
      snprintf(wifi_selected_ssid, sizeof(wifi_selected_ssid), "%s",
               saved_ssid);
      snprintf(wifi_password_input, sizeof(wifi_password_input), "%s",
               saved_pass);
      wifi_connect_to(saved_ssid, saved_pass);
    }
  }

  // Init Bluetooth (via ESP32-C6)
  // Note: Bluetooth runs on the same ESP32-C6 co-processor as WiFi
  if (bluetooth_init() != ESP_OK) {
    ESP_LOGW(TAG, "Bluetooth init failed - BT features will be unavailable");
    bluetooth_enabled = false;
  }

  // Init SD Card (SPI mode - compatible with ESP-Hosted SDIO)
  if (sd_card_init() != ESP_OK) {
    ESP_LOGW(TAG, "SD Card init failed - storage features limited");
  }

  // Init Audio (buzzer/speaker)
  audio_init();

  // Init hardware
  ESP_ERROR_CHECK(backlight_init());

  esp_lcd_panel_io_handle_t panel_io;
  esp_lcd_panel_handle_t panel_handle;
  ESP_ERROR_CHECK(display_init(&panel_io, &panel_handle));
  touch_init();

  // Init LVGL
  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = panel_io,
      .panel_handle = panel_handle,
      .buffer_size = LCD_H_RES * 50,
      .double_buffer = true,
      .hres = LCD_H_RES,
      .vres = LCD_V_RES,
      .monochrome = false,
      .color_format = LV_COLOR_FORMAT_RGB565,
      .rotation = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
      .flags = {.buff_dma = true, .buff_spiram = true, .sw_rotate = false},
  };
  const lvgl_port_display_dsi_cfg_t dsi_cfg = {
      .flags = {.avoid_tearing = false}};
  main_display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

  if (touch_handle) {
    const lvgl_port_touch_cfg_t touch_cfg = {.disp = main_display,
                                             .handle = touch_handle};
    lvgl_port_add_touch(&touch_cfg);
  }

  // Initialize Tribolonotus Pet Simulator
  ESP_LOGI(TAG, "Initializing Tribolonotus Pet Simulator...");
  pet_simulator_init();
  ESP_LOGI(TAG, "Pet Simulator initialized!");

  create_ui();

  // Initialize pet UI
  lv_obj_t *scr = lv_scr_act();
  ui_pet_init(scr);

  backlight_set(100);

  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "INIT COMPLETE - TRIBOLONOTUS SIMULATOR READY!");
  ESP_LOGI(TAG, "========================================");

  while (true) {
    update_status_bar();

    // Update pet simulator (appelé chaque seconde)
    pet_simulator_update();
    ui_pet_update();

#if CONFIG_BT_ENABLED
    // Update BLE scan results UI when scan completes (thread-safe approach)
    if (bt_scan_update_pending) {
      bt_scan_update_pending = false;
      if (lvgl_port_lock(100)) {
        update_bt_list();
        if (bt_status_label) {
          char status[64];
          snprintf(status, sizeof(status), "%d appareils BLE trouves",
                   bt_scan_count);
          lv_label_set_text(bt_status_label, status);
        }
        lvgl_port_unlock();
      }
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(500)); // Check more frequently
  }
}