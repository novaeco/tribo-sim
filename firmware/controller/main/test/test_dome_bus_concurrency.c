#include <stdbool.h>
#include <string.h>

#include "unity.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "drivers/dome_bus.h"
#include "include/config.h"
#include "dome_regs.h"
#include "driver/i2c_master.h"

static portMUX_TYPE s_stub_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_active_transactions = 0;
static int s_collision_count = 0;
static int s_read_calls = 0;
static int s_write_calls = 0;
static SemaphoreHandle_t s_done_sem = NULL;
static volatile bool s_http_ok = true;
static volatile bool s_sensor_ok = true;

static void stub_enter(void)
{
    portENTER_CRITICAL(&s_stub_lock);
    s_active_transactions++;
    if (s_active_transactions > 1) {
        s_collision_count++;
    }
    portEXIT_CRITICAL(&s_stub_lock);
}

static void stub_exit(void)
{
    portENTER_CRITICAL(&s_stub_lock);
    s_active_transactions--;
    portEXIT_CRITICAL(&s_stub_lock);
}

esp_err_t tca9548a_select(i2c_port_t port, uint8_t addr, uint8_t mask)
{
    (void)port;
    (void)addr;
    (void)mask;
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

esp_err_t dome_read_reg(i2c_port_t port, uint8_t addr, uint8_t reg, uint8_t* data, size_t len)
{
    (void)port;
    (void)addr;
    (void)reg;
    stub_enter();
    if (data && len > 0) {
        memset(data, 0, len);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    stub_exit();
    portENTER_CRITICAL(&s_stub_lock);
    s_read_calls++;
    portEXIT_CRITICAL(&s_stub_lock);
    return ESP_OK;
}

esp_err_t dome_write_reg(i2c_port_t port, uint8_t addr, uint8_t reg, const uint8_t* data, size_t len)
{
    (void)port;
    (void)addr;
    (void)reg;
    (void)data;
    (void)len;
    stub_enter();
    vTaskDelay(pdMS_TO_TICKS(2));
    stub_exit();
    portENTER_CRITICAL(&s_stub_lock);
    s_write_calls++;
    portEXIT_CRITICAL(&s_stub_lock);
    return ESP_OK;
}

static void http_loop_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 32 && s_http_ok; ++i) {
        esp_err_t err = dome_bus_select(TCA_CH_DOME0);
        if (err != ESP_OK) {
            s_http_ok = false;
            break;
        }
        uint8_t payload[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, (uint8_t)i };
        err = dome_bus_write(DOME_REG_BLOCK_CCT, payload, sizeof(payload));
        if (err != ESP_OK) {
            s_http_ok = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    xSemaphoreGive(s_done_sem);
    vTaskDelete(NULL);
}

static void sensor_loop_task(void *arg)
{
    (void)arg;
    uint8_t buf[4];
    for (int i = 0; i < 32 && s_sensor_ok; ++i) {
        esp_err_t err = dome_bus_select(TCA_CH_DOME0);
        if (err != ESP_OK) {
            s_sensor_ok = false;
            break;
        }
        err = dome_bus_read(DOME_REG_BLOCK_UVI, buf, sizeof(buf));
        if (err != ESP_OK) {
            s_sensor_ok = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    xSemaphoreGive(s_done_sem);
    vTaskDelete(NULL);
}

TEST_CASE("dome bus serializes concurrent users", "[dome][bus][integration]")
{
    s_collision_count = 0;
    s_active_transactions = 0;
    s_read_calls = 0;
    s_write_calls = 0;
    s_http_ok = true;
    s_sensor_ok = true;

    if (s_done_sem) {
        vSemaphoreDelete(s_done_sem);
    }
    s_done_sem = xSemaphoreCreateCounting(2, 0);
    TEST_ASSERT_NOT_NULL(s_done_sem);

    dome_bus_clear_degraded();
    TEST_ASSERT_EQUAL(ESP_OK, dome_bus_select(TCA_CH_DOME0));

    BaseType_t created = xTaskCreate(http_loop_task, "http_loop", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    TEST_ASSERT_EQUAL(pdPASS, created);
    created = xTaskCreate(sensor_loop_task, "sensor_loop", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    TEST_ASSERT_EQUAL(pdPASS, created);

    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(s_done_sem, pdMS_TO_TICKS(5000)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(s_done_sem, pdMS_TO_TICKS(5000)));

    vSemaphoreDelete(s_done_sem);
    s_done_sem = NULL;

    TEST_ASSERT_TRUE(s_http_ok);
    TEST_ASSERT_TRUE(s_sensor_ok);
    TEST_ASSERT_EQUAL(0, s_collision_count);
    TEST_ASSERT_TRUE(s_read_calls > 0);
    TEST_ASSERT_TRUE(s_write_calls > 0);
    TEST_ASSERT_FALSE(dome_bus_is_degraded());
}
