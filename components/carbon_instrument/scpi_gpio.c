#include "carbon_instrument.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "scpi_gpio";

static bool is_valid_pin(int pin)
{
    switch (pin) {
        case 2: case 4: case 5: case 13: case 14:
        case 18: case 19: case 21: case 22: case 23:
        case 25: case 26: case 27: case 32: case 33:
            return true;
        default:
            return false;
    }
}

static int gpio_set_handler(const char *cmd, char *r, size_t n)
{
    int pin, value;
    if (sscanf(cmd + 9, "%d %d", &pin, &value) != 2) {
        snprintf(r, n, "ERROR: Invalid GPIO:SET syntax");
        return strlen(r);
    }
    if (!is_valid_pin(pin)) { snprintf(r, n, "ERROR: Invalid pin %d", pin); return strlen(r); }
    if (value != 0 && value != 1) { snprintf(r, n, "ERROR: Value must be 0 or 1"); return strlen(r); }
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, value);
    ESP_LOGI(TAG, "GPIO%d = %d", pin, value);
    snprintf(r, n, "OK");
    return strlen(r);
}

static int gpio_get_handler(const char *cmd, char *r, size_t n)
{
    int pin;
    if (sscanf(cmd + 10, "%d", &pin) != 1) {
        snprintf(r, n, "ERROR: Invalid GPIO:GET? syntax");
        return strlen(r);
    }
    if (!is_valid_pin(pin)) { snprintf(r, n, "ERROR: Invalid pin %d", pin); return strlen(r); }
    snprintf(r, n, "%d", gpio_get_level(pin));
    return strlen(r);
}

static int gpio_config_handler(const char *cmd, char *r, size_t n)
{
    int pin;
    char mode_str[16];
    if (sscanf(cmd + 12, "%d %15s", &pin, mode_str) != 2) {
        snprintf(r, n, "ERROR: Invalid GPIO:CONFIG syntax");
        return strlen(r);
    }
    if (!is_valid_pin(pin)) { snprintf(r, n, "ERROR: Invalid pin %d", pin); return strlen(r); }
    gpio_mode_t mode;
    if (strcasecmp(mode_str, "INPUT") == 0)       mode = GPIO_MODE_INPUT;
    else if (strcasecmp(mode_str, "OUTPUT") == 0) mode = GPIO_MODE_OUTPUT;
    else { snprintf(r, n, "ERROR: Mode must be INPUT or OUTPUT"); return strlen(r); }
    gpio_set_direction(pin, mode);
    ESP_LOGI(TAG, "GPIO%d configured as %s", pin, mode_str);
    snprintf(r, n, "OK");
    return strlen(r);
}

void scpi_gpio_init(void)
{
    static const carbon_cmd_descriptor_t cmds[] = {
        {
            .scpi_command = "GPIO:SET",
            .type         = CARBON_CMD_WRITE,
            .group        = "GPIO",
            .description  = "Set digital output level",
            .params       = {
                { .name = "pin",   .type = CARBON_PARAM_INT, .min = 2, .max = 33,
                  .description = "GPIO pin number" },
                { .name = "value", .type = CARBON_PARAM_INT, .min = 0, .max = 1,
                  .description = "Output level (0 or 1)" },
            },
            .param_count  = 2,
            .timeout_ms   = 100,
            .handler      = gpio_set_handler,
        },
        {
            .scpi_command = "GPIO:GET?",
            .type         = CARBON_CMD_QUERY,
            .group        = "GPIO",
            .description  = "Read digital pin level",
            .params       = {
                { .name = "pin", .type = CARBON_PARAM_INT, .min = 2, .max = 33,
                  .description = "GPIO pin number" },
            },
            .param_count  = 1,
            .timeout_ms   = 100,
            .handler      = gpio_get_handler,
        },
        {
            .scpi_command = "GPIO:CONFIG",
            .type         = CARBON_CMD_WRITE,
            .group        = "GPIO",
            .description  = "Configure GPIO pin direction",
            .params       = {
                { .name = "pin",       .type = CARBON_PARAM_INT,  .min = 2, .max = 33,
                  .description = "GPIO pin number" },
                { .name = "direction", .type = CARBON_PARAM_ENUM,
                  .enum_values = {"INPUT", "OUTPUT"}, .enum_count = 2,
                  .description = "Pin direction" },
            },
            .param_count  = 2,
            .timeout_ms   = 100,
            .handler      = gpio_config_handler,
        },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        carbon_register_command(&cmds[i]);
    }
    ESP_LOGI(TAG, "GPIO commands registered");
}
