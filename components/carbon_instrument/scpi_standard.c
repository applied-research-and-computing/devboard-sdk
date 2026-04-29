#include "carbon_instrument.h"
#include "carbon_error_queue.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "scpi_std";

static uint8_t *s_status_byte = NULL;

void scpi_standard_set_status_source(uint8_t *ptr)
{
    s_status_byte = ptr;
}

static int idn_handler(const char *cmd, char *r, size_t n)
{
    const carbon_instrument_config_t *cfg = carbon_instrument_get_config();
    snprintf(r, n, "%s,%s,%s,%s",
             cfg->manufacturer, cfg->model, cfg->serial, cfg->firmware_version);
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
    if (s_status_byte != NULL) {
        *s_status_byte = 0;
    }
    carbon_clear_errors();
    snprintf(r, n, "OK");
    return strlen(r);
}

static int stb_handler(const char *cmd, char *r, size_t n)
{
    uint8_t stb = s_status_byte != NULL ? *s_status_byte : 0;
    snprintf(r, n, "%u", stb);
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
        { .scpi_command = "*STB?", .type = CARBON_CMD_QUERY, .group = "IEEE488",
          .description = "Read status byte",             .timeout_ms = 500,  .handler = stb_handler },
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
