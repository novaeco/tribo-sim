// Input subsystem declarations
//
// This component manages the Goodix GT911 touch panel.  It
// initialises the I2C driver, probes the GT911 for its runtime
// address, and sets up the esp_lcd_touch driver.  It also spawns
// the sensor task which polls the touch controller at regular
// intervals and updates global variables for the LVGL input driver.

#pragma once

// Initialise the I2C bus, discover the GT911 I2C address and
// configure the touch driver.  Must be called before registering
// the LVGL input device.
void touch_init(void);

// FreeRTOS task that reads data from the GT911 at a regular
// interval and updates the global touch state.  It should be
// created once from app_main() or an equivalent initialisation
// function.
void sensor_task(void *arg);
