/**
 * @file logging_cli.c
 * @brief CLI: logrb (ring-buffer) + loglvl (poziomy logów) + idempotentny REPL.
 */

#include "sdkconfig.h"
#include "infra_log_rb.h"
#include "ports/log_port.h"

#if CONFIG_INFRA_LOG_CLI

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "esp_console_usb_serial_jtag.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "LOGCLI"

/* ========================= logrb ========================= */

static int cmd_logrb(int argc, char** argv)
{
#if !CONFIG_INFRA_LOG_RINGBUF
    (void)argc; (void)argv;
    printf("logrb: ring-buffer disabled (set CONFIG_INFRA_LOG_RINGBUF=y)\n");
    return 0;
#else
    if (argc < 2) {
        printf("użycie: logrb {stat|clear|dump [--limit N]|tail [N]}\n");
        return 0;
    }

    if (strcmp(argv[1], "stat") == 0) {
        size_t cap=0, used=0; bool ov=false;
        infra_log_rb_stat(&cap, &used, &ov);
        printf("ringbuf: capacity=%uB used=%uB overflow=%s\n",
               (unsigned)cap, (unsigned)used, ov ? "yes":"no");
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0) {
        infra_log_rb_clear();
        printf("ringbuf: cleared\n");
        return 0;
    }

    if (strcmp(argv[1], "dump") == 0) {
        size_t cap=0, used=0; bool ov=false;
        infra_log_rb_stat(&cap, &used, &ov);

        size_t limit = used;
        if (argc >= 4 && strcmp(argv[2], "--limit") == 0) {
            limit = (size_t)strtoul(argv[3], NULL, 10);
            if (limit > used) limit = used;
        }

        char* buf = (char*)malloc(limit > 0 ? limit : 1);
        if (!buf) { printf("alloc failed\n"); return 1; }

        size_t got = 0;
        if (!infra_log_rb_snapshot(buf, limit, &got)) {
            free(buf);
            printf("snapshot failed\n"); return 1;
        }
        fwrite(buf, 1, got, stdout);
        free(buf);
        return 0;
    }

    if (strcmp(argv[1], "tail") == 0) {
        size_t n = CONFIG_INFRA_LOG_CLI_TAIL_DEFAULT;
        if (argc >= 3) {
            n = (size_t)strtoul(argv[2], NULL, 10);
            if (n == 0) n = 1;
        }

        size_t cap=0, used=0; bool ov=false;
        infra_log_rb_stat(&cap, &used, &ov);
        if (n > used) n = used;

        char* buf = (char*)malloc(n > 0 ? n : 1);
        if (!buf) { printf("alloc failed\n"); return 1; }

        size_t got = 0;
        if (!infra_log_rb_tail(buf, n, n, &got)) {
            free(buf);
            printf("tail failed\n"); return 1;
        }
        fwrite(buf, 1, got, stdout);
        free(buf);
        return 0;
    }

    printf("nieznana podkomenda: %s\n", argv[1]);
    return 0;
#endif // CONFIG_INFRA_LOG_RINGBUF
}

/* ========================= loglvl =========================
 * Zmiana poziomu logów w locie: loglvl <TAG|*> <E|W|I|D|V>
 */
static int cmd_loglvl(int argc, char** argv)
{
    if (argc < 3) {
        printf("użycie: loglvl <TAG|*> <E|W|I|D|V>\n");
        return 0;
    }
    const char* tag = argv[1];
    char lvlch = argv[2][0];

    esp_log_level_t lvl = ESP_LOG_INFO;
    switch (lvlch) {
        case 'E': lvl = ESP_LOG_ERROR;   break;
        case 'W': lvl = ESP_LOG_WARN;    break;
        case 'I': lvl = ESP_LOG_INFO;    break;
        case 'D': lvl = ESP_LOG_DEBUG;   break;
        case 'V': lvl = ESP_LOG_VERBOSE; break;
        default:
            printf("nieznany poziom: %s (użyj E/W/I/D/V)\n", argv[2]);
            return 0;
    }

    esp_log_level_set(tag, lvl);
    printf("log level for '%s' -> %c\n", tag, lvlch);
    return 0;
}

/* ============== idempotentna rejestracja komend ============== */

static bool s_cmds_registered = false;

esp_err_t infra_log_cli_register(void)
{
    if (s_cmds_registered) {
        ESP_LOGD(TAG, "commands already registered");
        return ESP_OK;
    }

    esp_console_register_help_command();

    const esp_console_cmd_t cmd_logrb_desc = {
        .command  = "logrb",
        .help     = "logrb stat|clear|dump [--limit N]|tail [N]",
        .hint     = NULL,
        .func     = &cmd_logrb,
        .argtable = NULL
    };
    const esp_console_cmd_t cmd_loglvl_desc = {
        .command  = "loglvl",
        .help     = "loglvl <TAG|*> <E|W|I|D|V>  (zmień poziom logów w locie)",
        .hint     = NULL,
        .func     = &cmd_loglvl,
        .argtable = NULL
    };

    // „Miękkie” rejestrowanie — bez abortu na duplikat
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_logrb_desc));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_loglvl_desc));

    s_cmds_registered = true;
    LOGI(TAG, "registered 'logrb' and 'loglvl' commands");
    return ESP_OK;
}

/* =================== idempotentny start REPL =================== */

static esp_console_repl_t* s_repl   = NULL;
static SemaphoreHandle_t   s_cli_mux = NULL;

static inline void init_cli_mutex_once(void)
{
    if (!s_cli_mux) {
        s_cli_mux = xSemaphoreCreateMutex();
    }
}

esp_err_t infra_log_cli_start_repl(void)
{
    // Zadbaj, by komendy były dostępne nawet jeśli REPL nie wystartuje
    infra_log_cli_register();

#if !CONFIG_INFRA_LOG_CLI_START_REPL
    LOGI(TAG, "registered CLI (REPL not started; CONFIG_INFRA_LOG_CLI_START_REPL=n)");
    return ESP_OK;
#else
    init_cli_mutex_once();
    if (!s_cli_mux) return ESP_ERR_NO_MEM;

    xSemaphoreTake(s_cli_mux, portMAX_DELAY);

    if (s_repl) {
        xSemaphoreGive(s_cli_mux);
        return ESP_OK; // już działa
    }

    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.prompt             = "esp> ";

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // USB-Serial-JTAG
    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_console_repl_t* repl = NULL;
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_new_repl_usb_serial_jtag() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_start_repl() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }

    s_repl = repl;
    LOGI(TAG, "USB-Serial-JTAG REPL started — wpisz komendy (np. 'logrb stat').");
    xSemaphoreGive(s_cli_mux);
    return ESP_OK;

#else
    // UART REPL
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    // uart_cfg.channel/tx/rx/baud zostają z DEF; można nadpisać wg potrzeb.

    uart_port_t cli_uart =
    #if defined(CONFIG_ESP_CONSOLE_UART_NUM)
        (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    #else
        UART_NUM_0;
    #endif

    // Jeśli driver UART jest już zajęty (ktoś inny wystartował REPL)
    if (uart_is_driver_installed(cli_uart)) {
        ESP_LOGW(TAG, "UART driver already installed — skip starting second REPL");
        xSemaphoreGive(s_cli_mux);
        return ESP_OK; // miękka degradacja
    }

    esp_console_repl_t* repl = NULL;
    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_new_repl_uart() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_start_repl() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }

    s_repl = repl;
    LOGI(TAG, "UART REPL started — wpisz komendy (np. 'logrb stat').");
    xSemaphoreGive(s_cli_mux);
    return ESP_OK;
#endif // CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#endif // CONFIG_INFRA_LOG_CLI_START_REPL
}

#else   // !CONFIG_INFRA_LOG_CLI

#include "esp_err.h"
esp_err_t infra_log_cli_register(void)   { return ESP_OK; }
esp_err_t infra_log_cli_start_repl(void) { return ESP_OK; }

#endif  // CONFIG_INFRA_LOG_CLI
