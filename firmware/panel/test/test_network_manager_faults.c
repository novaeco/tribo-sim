#include "unity.h"

#include <stdint.h>
#include <string.h>

#include "network_manager.h"

typedef struct {
    bool fail_timer_create;
    bool fail_task_create;
    uint32_t wifi_init_calls;
    uint32_t wifi_start_calls;
    uint32_t wifi_stop_calls;
    uint32_t wifi_deinit_calls;
    uint32_t wifi_connect_calls;
    uint32_t wifi_disconnect_calls;
    uint32_t timer_create_calls;
    uint32_t timer_stop_calls;
    uint32_t timer_delete_calls;
    uint32_t task_create_calls;
    uint32_t task_delete_calls;
    uint32_t event_register_calls;
    uint32_t event_unregister_calls;
} fake_ops_ctx_t;

static fake_ops_ctx_t s_fake_ops_ctx;

static esp_err_t fake_wifi_init(const wifi_init_config_t *config)
{
    (void)config;
    s_fake_ops_ctx.wifi_init_calls++;
    return ESP_OK;
}

static esp_err_t fake_wifi_set_mode(wifi_mode_t mode)
{
    (void)mode;
    return ESP_OK;
}

static esp_err_t fake_wifi_set_config(wifi_interface_t interface, wifi_config_t *config)
{
    (void)interface;
    (void)config;
    return ESP_OK;
}

static esp_err_t fake_wifi_start(void)
{
    s_fake_ops_ctx.wifi_start_calls++;
    return ESP_OK;
}

static esp_err_t fake_wifi_stop(void)
{
    s_fake_ops_ctx.wifi_stop_calls++;
    return ESP_OK;
}

static esp_err_t fake_wifi_deinit(void)
{
    s_fake_ops_ctx.wifi_deinit_calls++;
    return ESP_OK;
}

static esp_err_t fake_wifi_connect(void)
{
    s_fake_ops_ctx.wifi_connect_calls++;
    return ESP_OK;
}

static esp_err_t fake_wifi_disconnect(void)
{
    s_fake_ops_ctx.wifi_disconnect_calls++;
    return ESP_OK;
}

static esp_err_t fake_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_timer)
{
    (void)args;
    s_fake_ops_ctx.timer_create_calls++;
    if (s_fake_ops_ctx.fail_timer_create) {
        return ESP_ERR_NO_MEM;
    }
    if (out_timer) {
        *out_timer = (esp_timer_handle_t)0x1;
    }
    return ESP_OK;
}

static esp_err_t fake_timer_stop(esp_timer_handle_t timer)
{
    (void)timer;
    s_fake_ops_ctx.timer_stop_calls++;
    return ESP_OK;
}

static esp_err_t fake_timer_delete(esp_timer_handle_t timer)
{
    (void)timer;
    s_fake_ops_ctx.timer_delete_calls++;
    return ESP_OK;
}

static BaseType_t fake_task_create(TaskFunction_t task,
                                   const char *name,
                                   const uint32_t stack_depth,
                                   void *params,
                                   UBaseType_t priority,
                                   TaskHandle_t *out_handle,
                                   const BaseType_t core_id)
{
    (void)task;
    (void)name;
    (void)stack_depth;
    (void)params;
    (void)priority;
    (void)core_id;
    s_fake_ops_ctx.task_create_calls++;
    if (s_fake_ops_ctx.fail_task_create) {
        return pdFAIL;
    }
    if (out_handle) {
        *out_handle = (TaskHandle_t)0x1;
    }
    return pdPASS;
}

static void fake_task_delete(TaskHandle_t handle)
{
    (void)handle;
    s_fake_ops_ctx.task_delete_calls++;
}

static esp_err_t fake_event_register(esp_event_base_t event_base,
                                     int32_t event_id,
                                     esp_event_handler_t handler,
                                     void *handler_arg,
                                     esp_event_handler_instance_t *instance_out)
{
    (void)event_base;
    (void)event_id;
    (void)handler;
    (void)handler_arg;
    s_fake_ops_ctx.event_register_calls++;
    if (instance_out) {
        *instance_out = (esp_event_handler_instance_t)(uintptr_t)(0x10 + s_fake_ops_ctx.event_register_calls);
    }
    return ESP_OK;
}

static esp_err_t fake_event_unregister(esp_event_base_t event_base,
                                       int32_t event_id,
                                       esp_event_handler_instance_t instance)
{
    (void)event_base;
    (void)event_id;
    (void)instance;
    s_fake_ops_ctx.event_unregister_calls++;
    return ESP_OK;
}

void setUp(void)
{
    memset(&s_fake_ops_ctx, 0, sizeof(s_fake_ops_ctx));
    network_manager_runtime_ops_t ops = {
        .wifi_init = fake_wifi_init,
        .wifi_set_mode = fake_wifi_set_mode,
        .wifi_set_config = fake_wifi_set_config,
        .wifi_start = fake_wifi_start,
        .wifi_stop = fake_wifi_stop,
        .wifi_deinit = fake_wifi_deinit,
        .wifi_connect = fake_wifi_connect,
        .wifi_disconnect = fake_wifi_disconnect,
        .task_create_pinned_to_core = fake_task_create,
        .task_delete = fake_task_delete,
        .timer_create = fake_timer_create,
        .timer_stop = fake_timer_stop,
        .timer_delete = fake_timer_delete,
        .event_handler_register = fake_event_register,
        .event_handler_unregister = fake_event_unregister,
    };
    network_manager_use_custom_runtime_ops(&ops);
}

void tearDown(void)
{
    network_manager_stop();
    network_manager_use_custom_runtime_ops(NULL);
}

static void prepare_basic_config(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strlcpy(cfg->ssid, "test", sizeof(cfg->ssid));
    strlcpy(cfg->password, "password", sizeof(cfg->password));
    strlcpy(cfg->controller_host, "controller", sizeof(cfg->controller_host));
    cfg->controller_port = 1234;
}

TEST_CASE("network_manager_start stops wifi when reconnect timer creation fails", "[network][fault]")
{
    s_fake_ops_ctx.fail_timer_create = true;

    app_config_t cfg;
    prepare_basic_config(&cfg);

    esp_err_t err = network_manager_start(&cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_init_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_start_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_deinit_calls);
    TEST_ASSERT_EQUAL_UINT32(2, s_fake_ops_ctx.event_register_calls);
    TEST_ASSERT_EQUAL_UINT32(2, s_fake_ops_ctx.event_unregister_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.timer_create_calls);
    TEST_ASSERT_EQUAL_UINT32(0, s_fake_ops_ctx.timer_stop_calls);
    TEST_ASSERT_EQUAL_UINT32(0, s_fake_ops_ctx.timer_delete_calls);
    TEST_ASSERT_EQUAL_UINT32(0, s_fake_ops_ctx.task_create_calls);
}

TEST_CASE("network_manager_start unregisters handlers when task creation fails", "[network][fault]")
{
    s_fake_ops_ctx.fail_task_create = true;

    app_config_t cfg;
    prepare_basic_config(&cfg);

    esp_err_t err = network_manager_start(&cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_init_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_start_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.wifi_deinit_calls);
    TEST_ASSERT_EQUAL_UINT32(2, s_fake_ops_ctx.event_register_calls);
    TEST_ASSERT_EQUAL_UINT32(2, s_fake_ops_ctx.event_unregister_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.timer_create_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.timer_stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.timer_delete_calls);
    TEST_ASSERT_EQUAL_UINT32(1, s_fake_ops_ctx.task_create_calls);
    TEST_ASSERT_EQUAL_UINT32(0, s_fake_ops_ctx.task_delete_calls);
}
