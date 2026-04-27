#include "carbon_instrument.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "scpi_uart";

#define UART_PORT    UART_NUM_1
#define UART_TX_PIN  17
#define UART_RX_PIN  16
#define UART_BUF_SIZE 1024

static bool uart_initialized = false;

static int uart_write_handler(const char *cmd, char *r, size_t n)
{
    if (!uart_initialized) { snprintf(r, n, "ERROR: UART not initialized"); return strlen(r); }
    const char *data = cmd + 11;
    int len = strlen(data);
    if (len == 0) { snprintf(r, n, "ERROR: No data to write"); return strlen(r); }
    int written = uart_write_bytes(UART_PORT, data, len);
    if (written < 0) { snprintf(r, n, "ERROR: UART write failed"); return strlen(r); }
    ESP_LOGI(TAG, "Wrote %d bytes to UART", written);
    snprintf(r, n, "OK");
    return strlen(r);
}

static int uart_read_handler(const char *cmd, char *r, size_t n)
{
    if (!uart_initialized) { snprintf(r, n, "ERROR: UART not initialized"); return strlen(r); }
    uint8_t buf[256];
    int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(100));
    if (len < 0) { snprintf(r, n, "ERROR: UART read failed"); return strlen(r); }
    if (len == 0) { r[0] = '\0'; return 0; }
    buf[len] = '\0';
    bool printable = true;
    for (int i = 0; i < len; i++) {
        if (buf[i] < 32 || buf[i] > 126) { printable = false; break; }
    }
    if (printable) {
        snprintf(r, n, "%s", (char *)buf);
    } else {
        int off = 0;
        for (int i = 0; i < len && off < (int)n - 3; i++) {
            off += snprintf(r + off, n - off, "%02X", buf[i]);
        }
    }
    ESP_LOGI(TAG, "Read %d bytes from UART", len);
    return strlen(r);
}

static int uart_config_handler(const char *cmd, char *r, size_t n)
{
    if (!uart_initialized) { snprintf(r, n, "ERROR: UART not initialized"); return strlen(r); }
    int baud, data_bits, stop_bits;
    char parity_str[8];
    if (sscanf(cmd + 12, "%d %d %7s %d", &baud, &data_bits, parity_str, &stop_bits) != 4) {
        snprintf(r, n, "ERROR: Invalid UART:CONFIG syntax");
        return strlen(r);
    }
    uart_word_length_t db;
    switch (data_bits) {
        case 5: db = UART_DATA_5_BITS; break;
        case 6: db = UART_DATA_6_BITS; break;
        case 7: db = UART_DATA_7_BITS; break;
        case 8: db = UART_DATA_8_BITS; break;
        default: snprintf(r, n, "ERROR: Data bits must be 5-8"); return strlen(r);
    }
    uart_parity_t parity;
    if (strcasecmp(parity_str, "NONE") == 0)      parity = UART_PARITY_DISABLE;
    else if (strcasecmp(parity_str, "EVEN") == 0) parity = UART_PARITY_EVEN;
    else if (strcasecmp(parity_str, "ODD") == 0)  parity = UART_PARITY_ODD;
    else { snprintf(r, n, "ERROR: Parity must be NONE, EVEN, or ODD"); return strlen(r); }
    uart_stop_bits_t sb;
    if (stop_bits == 1)      sb = UART_STOP_BITS_1;
    else if (stop_bits == 2) sb = UART_STOP_BITS_2;
    else { snprintf(r, n, "ERROR: Stop bits must be 1 or 2"); return strlen(r); }

    if (uart_set_baudrate(UART_PORT, baud)      != ESP_OK ||
        uart_set_word_length(UART_PORT, db)     != ESP_OK ||
        uart_set_parity(UART_PORT, parity)      != ESP_OK ||
        uart_set_stop_bits(UART_PORT, sb)       != ESP_OK)
    {
        snprintf(r, n, "ERROR: UART config failed");
        return strlen(r);
    }
    ESP_LOGI(TAG, "UART configured: %d %d%s%d", baud, data_bits, parity_str, stop_bits);
    snprintf(r, n, "OK");
    return strlen(r);
}

void scpi_uart_init(void)
{
    if (!uart_initialized) {
        uart_config_t cfg = {
            .baud_rate  = 115200,
            .data_bits  = UART_DATA_8_BITS,
            .parity     = UART_PARITY_DISABLE,
            .stop_bits  = UART_STOP_BITS_1,
            .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        esp_err_t err = uart_driver_install(UART_PORT, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
        if (err != ESP_OK) { ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err)); return; }
        err = uart_param_config(UART_PORT, &cfg);
        if (err != ESP_OK) { uart_driver_delete(UART_PORT); return; }
        err = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) { uart_driver_delete(UART_PORT); return; }
        uart_initialized = true;
        ESP_LOGI(TAG, "UART%d initialized (TX=%d, RX=%d, 115200 8N1)", UART_PORT, UART_TX_PIN, UART_RX_PIN);
    }

    static const char *parity_enums[] = {"NONE", "EVEN", "ODD"};

    static const carbon_cmd_descriptor_t cmds[] = {
        {
            .scpi_command = "UART:WRITE",
            .type         = CARBON_CMD_WRITE,
            .group        = "UART",
            .description  = "Write data to UART1",
            .params       = {
                { .name = "data", .type = CARBON_PARAM_STRING, .description = "Data string to send" },
            },
            .param_count  = 1,
            .timeout_ms   = 500,
            .handler      = uart_write_handler,
        },
        {
            .scpi_command = "UART:READ?",
            .type         = CARBON_CMD_QUERY,
            .group        = "UART",
            .description  = "Read available bytes from UART1 (100ms timeout)",
            .param_count  = 0,
            .timeout_ms   = 500,
            .handler      = uart_read_handler,
        },
        {
            .scpi_command = "UART:CONFIG",
            .type         = CARBON_CMD_WRITE,
            .group        = "UART",
            .description  = "Configure UART1 parameters",
            .params       = {
                { .name = "baud",      .type = CARBON_PARAM_INT,    .min = 300,  .max = 921600,
                  .description = "Baud rate" },
                { .name = "data_bits", .type = CARBON_PARAM_INT,    .min = 5,    .max = 8,
                  .description = "Data bits (5-8)" },
                { .name = "parity",    .type = CARBON_PARAM_ENUM,
                  .enum_values = {"NONE", "EVEN", "ODD"}, .enum_count = 3,
                  .description = "Parity" },
                { .name = "stop_bits", .type = CARBON_PARAM_INT,    .min = 1,    .max = 2,
                  .description = "Stop bits (1 or 2)" },
            },
            .param_count  = 4,
            .timeout_ms   = 500,
            .handler      = uart_config_handler,
        },
    };

    (void)parity_enums;
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        carbon_register_command(&cmds[i]);
    }
    ESP_LOGI(TAG, "UART commands registered");
}
