/**
 * @file lv_conf.h
 * LVGL 9.x Configuration for Tribo-Sim ESP32-P4 Project
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 16 (RGB565) for optimal performance */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color if display requires it */
#define LV_COLOR_16_SWAP 0

/*====================
   MEMORY SETTINGS
 *====================*/

/* Use custom malloc/free - ESP-IDF heap */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

/* Size of the memory used by `lv_malloc` in kilobytes (>= 2kB) */
#define LV_MEM_SIZE (64 * 1024U)

/* Use PSRAM for LVGL memory pool */
#define LV_MEM_ADR 0

/* Memory pool expansion when LVGL runs out of memory */
#define LV_MEM_POOL_EXPAND_SIZE 0

/*====================
   DISPLAY SETTINGS
 *====================*/

/* Default display refresh period in milliseconds */
#define LV_DEF_REFR_PERIOD 16

/* Dot Per Inch: used for lv_pct */
#define LV_DPI_DEF 130

/*====================
   TICK SETTINGS
 *====================*/

/* Use a custom tick source - FreeRTOS provides it */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR ((esp_timer_get_time() / 1000ULL))
#endif

/*====================
   FEATURE CONFIG
 *====================*/

/* Enable animations */
#define LV_USE_ANIMATION 1

/* Enable shadow effects */
#define LV_USE_SHADOW 1

/* Enable blend modes for performance */
#define LV_USE_BLEND_MODES 1

/* Enable opacity for objects */
#define LV_USE_OPA_SCALE 1

/* Enable image transformations */
#define LV_USE_IMG_TRANSFORM 1

/*====================
   LOGGING
 *====================*/

#define LV_USE_LOG 1
#if LV_USE_LOG
    /* Log level: TRACE, INFO, WARN, ERROR, USER, NONE */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

    /* Print function - use ESP_LOG */
    #define LV_LOG_PRINTF 0
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_TRACE_MEM 0
    #define LV_LOG_TRACE_TIMER 0
    #define LV_LOG_TRACE_INDEV 0
    #define LV_LOG_TRACE_DISP_REFR 0
    #define LV_LOG_TRACE_EVENT 0
    #define LV_LOG_TRACE_OBJ_CREATE 0
    #define LV_LOG_TRACE_LAYOUT 0
    #define LV_LOG_TRACE_ANIM 0
#endif

/*====================
   ASSERT CONFIG
 *====================*/

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/*====================
   FONT SETTINGS
 *====================*/

/* Montserrat fonts - various sizes */
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Enable font subpixel rendering */
#define LV_FONT_SUBPX_BGR 0

/*====================
   TEXT SETTINGS
 *====================*/

/* String break characters */
#define LV_TXT_BREAK_CHARS " ,.;:-_"

/* Long text line break behavior */
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3

/*====================
   WIDGET CONFIG
 *====================*/

/* Basic widgets - all enabled */
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMAGE 1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LABEL 1
#define LV_USE_LED 0
#define LV_USE_LINE 1
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 1
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 1
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 1
#define LV_USE_SWITCH 1
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/*====================
   LAYOUT CONFIG
 *====================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*====================
   THEME CONFIG
 *====================*/

/* Default theme - Material design inspired */
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

/* Simple theme for basic styling */
#define LV_USE_THEME_SIMPLE 0

/*====================
   FILE SYSTEM CONFIG
 *====================*/

/* Enable POSIX file system (SPIFFS/FAT) */
#define LV_USE_FS_POSIX 1
#if LV_USE_FS_POSIX
    #define LV_FS_POSIX_LETTER 'S'
    #define LV_FS_POSIX_PATH "/spiffs"
    #define LV_FS_POSIX_CACHE_SIZE 0
#endif

/*====================
   IMAGE DECODERS
 *====================*/

/* Built-in image decoder for C arrays */
#define LV_USE_DRAW_SW 1

/* PNG decoder */
#define LV_USE_LODEPNG 0

/* BMP decoder */
#define LV_USE_BMP 0

/* SJPG decoder */
#define LV_USE_SJPG 0

/* GIF decoder */
#define LV_USE_GIF 0

/*====================
   OTHER SETTINGS
 *====================*/

/* Async call max queue size */
#define LV_USE_ASYNC_CALL 1

/* Object groups for keyboard/encoder navigation */
#define LV_USE_GROUP 1

/* Garbage collector settings */
#define LV_GC_INCLUDE "gc.h"

/* GPU acceleration - use DMA2D on ESP32-P4 */
#define LV_USE_DRAW_DMA2D 0

/* Snapshot feature */
#define LV_USE_SNAPSHOT 0

/* Monkey testing */
#define LV_USE_MONKEY 0

/* Profiler */
#define LV_USE_PROFILER 0

/* Sysmon - system resource monitor */
#define LV_USE_SYSMON 0

/* Performance monitor */
#define LV_USE_PERF_MONITOR 0

/* Memory monitor */
#define LV_USE_MEM_MONITOR 0

#endif /* LV_CONF_H */
