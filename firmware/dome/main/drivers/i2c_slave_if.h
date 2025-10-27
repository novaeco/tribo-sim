#pragma once
#include "driver/i2c.h"
#include <stddef.h>

/**
 * @brief Initialise the I2C slave peripheral.
 *
 * This function must be called once before using ::i2c_slave_if_read or
 * ::i2c_slave_if_write. For a software reboot or when the slave interface is
 * no longer required, call ::i2c_slave_if_deinit to release the driver.
 */
esp_err_t i2c_slave_if_init(i2c_port_t port, int sda, int scl, uint8_t addr);

/**
 * @brief Read bytes from the I2C slave FIFO.
 *
 * @return Number of bytes read on success, or a negative ::esp_err_t value on
 *         failure (e.g. ::ESP_ERR_INVALID_STATE if initialisation is missing).
 */
int       i2c_slave_if_read(uint8_t* buf, size_t maxlen, TickType_t to);

/**
 * @brief Write bytes to the I2C slave FIFO.
 *
 * @return Number of bytes written on success, or a negative ::esp_err_t value
 *         on failure (e.g. ::ESP_ERR_INVALID_STATE if initialisation is missing).
 */
int       i2c_slave_if_write(const uint8_t* buf, size_t len, TickType_t to);

/**
 * @brief Release the I2C slave driver previously initialised with
 *        ::i2c_slave_if_init.
 */
esp_err_t i2c_slave_if_deinit(void);
