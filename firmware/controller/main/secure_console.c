#include "secure_console.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_err.h"
#include "net/credentials.h"
#include "sdkconfig.h"

static const char *TAG = "secure_console";
static bool s_console_started = false;
static esp_console_repl_t *s_repl_handle = NULL;

static int token_cmd(int argc, char **argv)
{
    bool rotate = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rotate") == 0 || strcmp(argv[i], "-r") == 0) {
            rotate = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: token [--rotate]\n");
            printf("  --rotate  Invalide le secret courant et affiche un nouveau jeton.\n");
            return 0;
        } else {
            printf("Option inconnue: %s\n", argv[i]);
            printf("Utilisez --help pour l'aide.\n");
            return 1;
        }
    }

    if (rotate) {
        esp_err_t err = credentials_rotate(false, true);
        if (err != ESP_OK) {
            printf("Rotation du jeton échouée: %s\n", esp_err_to_name(err));
            return (int)err;
        }
    }

    const char *token = credentials_bootstrap_token();
    if (!token) {
        printf("Aucun jeton bootstrap disponible. Utilisez --rotate pour en générer un nouveau.\n");
        return 1;
    }

    printf("HTTP API bootstrap token: %s\n", token);
    printf("Conservez ce secret dans un coffre sécurisé; il ne sera pas affiché de nouveau.\n");
    fflush(stdout);
    return 0;
}

static esp_err_t register_commands(void)
{
    const esp_console_cmd_t token_command = {
        .command = "token",
        .help = "Affiche ou régénère le jeton bootstrap HTTP (commande locale uniquement).",
        .hint = "[--rotate]",
        .func = &token_cmd,
        .argtable = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&token_command), TAG, "register token cmd failed");
    return ESP_OK;
}

esp_err_t secure_console_start(void)
{
    if (s_console_started) {
        return ESP_OK;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "secure> ";
    repl_config.max_cmdline_length = 256;
    repl_config.max_cmdline_args = 4;

#if CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t dev_config = ESP_CONSOLE_DEV_USB_CDC_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_cdc(&dev_config, &repl_config, &s_repl_handle), TAG, "USB CDC REPL init failed");
#else
    esp_console_dev_uart_config_t dev_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(&dev_config, &repl_config, &s_repl_handle), TAG, "UART REPL init failed");
#endif

    ESP_RETURN_ON_ERROR(esp_console_register_help_command(), TAG, "help cmd registration failed");
    ESP_RETURN_ON_ERROR(register_commands(), TAG, "command registration failed");
    ESP_RETURN_ON_ERROR(esp_console_start_repl(s_repl_handle), TAG, "start repl failed");
    s_console_started = true;
    ESP_LOGI(TAG, "Console de maintenance sécurisée initialisée");
    return ESP_OK;
}
