/**
 * @file logging_cli.c
 * @brief Konsolowa komenda `logrb`: stat | clear | dump [--limit N] | tail [N].
 *
 * @dot
 * digraph CLI {
 *   rankdir=LR; node [shape=box, fontsize=10];
 *   Host -> "idf_monitor/REPL" -> "esp_console" -> "cmd_logrb()" -> "infra_log_rb_*()";
 * }
 * @enddot
 */

#include "infra_log_rb.h"
#include "ports/log_port.h"

#if CONFIG_INFRA_LOG_CLI

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAG "LOGCLI"

static int cmd_logrb(int argc, char** argv)
{
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
}

void infra_log_cli_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "logrb",
        .help    = "logrb stat|clear|dump [--limit N]|tail [N]",
        .hint    = NULL,
        .func    = &cmd_logrb,
        .argtable= NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    LOGI(TAG, "registered 'logrb' command");
}

#if CONFIG_INFRA_LOG_CLI_START_REPL
void infra_log_cli_start_repl(void)
{
    // Standardowa konfiguracja REPL przez UART0 (baud 115200)
    esp_console_repl_t* repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    uart_cfg.channel = UART_NUM_0;
    uart_cfg.tx_gpio_num = -1;
    uart_cfg.rx_gpio_num = -1;
    uart_cfg.baud_rate   = 115200;

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));
    infra_log_cli_register();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    LOGI(TAG, "UART REPL started — wpisz 'logrb stat|dump|tail|clear'");
}
#else
void infra_log_cli_start_repl(void) { /* no-op, REPL niewłączony */ }
#endif

#else  // !CONFIG_INFRA_LOG_CLI
void infra_log_cli_register(void) {}
void infra_log_cli_start_repl(void) {}
#endif
