#include "carbon_instrument.h"
#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "scpi_std";

#define MANUFACTURER     "CARBON"
#define MODEL            "ESP32-INSTRUMENT"
#define FIRMWARE_VERSION "v1.0.0"

#ifndef CONFIG_DEVICE_SERIAL
#define DEVICE_SERIAL "SN12345"
#else
#define DEVICE_SERIAL CONFIG_DEVICE_SERIAL
#endif

static int idn_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, MANUFACTURER "," MODEL "," DEVICE_SERIAL "," FIRMWARE_VERSION);
    return strlen(r);
}

static int rst_handler(const char *cmd, char *r, size_t n)
{
    ESP_LOGI(TAG, "Device reset");
    snprintf(r, n, "OK");
    return strlen(r);
}

static int cls_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, "OK");
    return strlen(r);
}

static int esr_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, "0");
    return strlen(r);
}

static int ese_query_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, "0");
    return strlen(r);
}

static int ese_write_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, "OK");
    return strlen(r);
}

static int opc_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, "1");
    return strlen(r);
}

static int tst_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, "0");
    return strlen(r);
}

static int wai_handler(const char *cmd, char *r, size_t n)
{
    snprintf(r, n, "OK");
    return strlen(r);
}

void scpi_standard_init(void)
{
    static const carbon_cmd_descriptor_t cmds[] = {
        { .scpi_command = "*IDN?", .type = CARBON_CMD_QUERY, .group = "IEEE488",
          .description = "Identify instrument",          .timeout_ms = 500,  .handler = idn_handler },
        { .scpi_command = "*RST",  .type = CARBON_CMD_WRITE, .group = "IEEE488",
          .description = "Reset to defaults",            .timeout_ms = 1000, .handler = rst_handler },
        { .scpi_command = "*CLS",  .type = CARBON_CMD_WRITE, .group = "IEEE488",
          .description = "Clear status registers",       .timeout_ms = 500,  .handler = cls_handler },
        { .scpi_command = "*ESR?", .type = CARBON_CMD_QUERY, .group = "IEEE488",
          .description = "Read event status register",   .timeout_ms = 500,  .handler = esr_handler },
        { .scpi_command = "*ESE?", .type = CARBON_CMD_QUERY, .group = "IEEE488",
          .description = "Read event status enable",     .timeout_ms = 500,  .handler = ese_query_handler },
        { .scpi_command = "*ESE",  .type = CARBON_CMD_WRITE, .group = "IEEE488",
          .description = "Set event status enable mask", .timeout_ms = 500,  .handler = ese_write_handler,
          .params = {{ .name = "mask", .type = CARBON_PARAM_INT, .min = 0, .max = 255,
                       .description = "Enable register bitmask" }},
          .param_count = 1 },
        { .scpi_command = "*OPC?", .type = CARBON_CMD_QUERY, .group = "IEEE488",
          .description = "Operation complete query",     .timeout_ms = 500,  .handler = opc_handler },
        { .scpi_command = "*TST?", .type = CARBON_CMD_QUERY, .group = "IEEE488",
          .description = "Self-test query",              .timeout_ms = 2000, .handler = tst_handler },
        { .scpi_command = "*WAI",  .type = CARBON_CMD_WRITE, .group = "IEEE488",
          .description = "Wait to continue",             .timeout_ms = 500,  .handler = wai_handler },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        carbon_register_command(&cmds[i]);
    }
    ESP_LOGI(TAG, "Standard commands registered");
}
