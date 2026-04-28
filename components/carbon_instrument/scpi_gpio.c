#include "carbon_instrument.h"
#include "carbon_response.h"
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

static int gpio_set_handler_v2(const carbon_parsed_param_t *params, int param_count,
                               char *r, size_t n)
{
    (void)param_count;
    int pin   = params[0].int_val;
    int value = params[1].int_val;
    /* Range (2-33) already validated by the SDK; check for safe writable pins. */
    if (!is_valid_pin(pin)) {
        return carbon_respond_error(r, n, 2, "invalid pin");
    }
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(pin, value);
    ESP_LOGI(TAG, "GPIO%d = %d", pin, value);
    return carbon_respond_enum(r, n, "OK");
}

static int gpio_get_handler(const char *cmd, char *r, size_t n)
{
    int pin;
    if (sscanf(cmd + 10, "%d", &pin) != 1) {
        return carbon_respond_error(r, n, 2, "invalid GPIO:GET? syntax");
    }
    if (!is_valid_pin(pin)) { return carbon_respond_error(r, n, 2, "invalid pin"); }
    return carbon_respond_int(r, n, gpio_get_level(pin));
}

static int gpio_config_handler(const char *cmd, char *r, size_t n)
{
    int pin;
    char mode_str[16];
    if (sscanf(cmd + 12, "%d %15s", &pin, mode_str) != 2) {
        return carbon_respond_error(r, n, 2, "invalid GPIO:CONFIG syntax");
    }
    if (!is_valid_pin(pin)) { return carbon_respond_error(r, n, 2, "invalid pin"); }
    gpio_mode_t mode;
    if (strcasecmp(mode_str, "INPUT") == 0)       mode = GPIO_MODE_INPUT;
    else if (strcasecmp(mode_str, "OUTPUT") == 0) mode = GPIO_MODE_INPUT_OUTPUT;
    else { return carbon_respond_error(r, n, 2, "mode must be INPUT or OUTPUT"); }
    gpio_set_direction(pin, mode);
    ESP_LOGI(TAG, "GPIO%d configured as %s", pin, mode_str);
    return carbon_respond_enum(r, n, "OK");
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
                  .description = "GPIO pin number", .default_value = "2" },
                { .name = "value", .type = CARBON_PARAM_INT, .min = 0, .max = 1,
                  .description = "Output level (0 or 1)", .default_value = "0" },
            },
            .param_count  = 2,
            .timeout_ms   = 100,
            .handler_v2   = gpio_set_handler_v2,
        },
        {
            .scpi_command = "GPIO:GET?",
            .type         = CARBON_CMD_QUERY,
            .group        = "GPIO",
            .description  = "Read digital pin level",
            .params       = {
                { .name = "pin", .type = CARBON_PARAM_INT, .min = 2, .max = 33,
                  .description = "GPIO pin number", .default_value = "2" },
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
                  .description = "GPIO pin number", .default_value = "2" },
                { .name = "direction", .type = CARBON_PARAM_ENUM,
                  .enum_values = {"INPUT", "OUTPUT"}, .enum_count = 2,
                  .description = "Pin direction", .default_value = "OUTPUT" },
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
