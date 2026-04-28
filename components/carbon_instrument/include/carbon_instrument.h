#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define CARBON_MAX_PARAMS      8
#define CARBON_MAX_ENUM_VALUES 16

typedef enum {
    CARBON_PARAM_INT,
    CARBON_PARAM_FLOAT,
    CARBON_PARAM_STRING,
    CARBON_PARAM_BOOL,
    CARBON_PARAM_ENUM,
} carbon_param_type_t;

typedef enum {
    CARBON_CMD_WRITE,
    CARBON_CMD_QUERY,
} carbon_cmd_type_t;

// cmd: normalized SCPI string (uppercased mnemonic, args preserved)
// response: buffer to write response into
// response_max: size of response buffer
// returns: byte length of response written (0 = no response)
typedef int (*carbon_cmd_handler_t)(const char *cmd, char *response, size_t response_max);

// Parsed value for a single parameter, populated by the SDK before handler_v2 is called.
// For CARBON_PARAM_ENUM, int_val holds the matched index into descriptor->enum_values[].
typedef struct {
    const char         *name;
    carbon_param_type_t type;
    union {
        int         int_val;
        float       float_val;
        bool        bool_val;
        const char *str_val;
    };
} carbon_parsed_param_t;

// params: array of parsed parameters (length == param_count from descriptor)
// returns: byte length of response written (0 = no response)
typedef int (*carbon_cmd_handler_v2_t)(const carbon_parsed_param_t *params,
                                       int param_count,
                                       char *response,
                                       size_t response_max);

typedef struct {
    const char         *name;
    carbon_param_type_t type;
    const char         *description;
    double              min;
    double              max;
    const char         *enum_values[CARBON_MAX_ENUM_VALUES];
    int                 enum_count;
    const char         *default_value;
} carbon_param_t;

typedef struct {
    const char             *scpi_command;  // uppercase mnemonic, e.g. "GPIO:SET" or "*IDN?"
    carbon_cmd_type_t       type;
    const char             *group;
    const char             *description;
    carbon_param_t          params[CARBON_MAX_PARAMS];
    int                     param_count;
    int                     timeout_ms;
    carbon_cmd_handler_t    handler;    // set this OR handler_v2, not both
    carbon_cmd_handler_v2_t handler_v2; // SDK parses params before calling; NULL = use handler
} carbon_cmd_descriptor_t;

// Device identity. Any NULL field falls back to the Kconfig/firmware default.
typedef struct {
    const char *manufacturer;
    const char *model;
    const char *serial;
    const char *firmware_version;
} carbon_instrument_config_t;

// Optional: override device identity before carbon_instrument_start().
// Any non-NULL field in config replaces the default.
void carbon_instrument_init(const carbon_instrument_config_t *config);

// Returns the active identity configuration.
const carbon_instrument_config_t *carbon_instrument_get_config(void);

// Register a command with the instrument registry.
// desc must point to storage that outlives the call (use static const).
esp_err_t carbon_register_command(const carbon_cmd_descriptor_t *desc);

// Register a callback invoked when the client sends an IEEE 488.1 GET
// (Group Execute Trigger) via the HiSLIP TRIGGER message.
// Pass NULL to clear a previously registered callback.
void carbon_register_trigger(void (*callback)(void));

// Register built-in commands and start the HiSLIP server.
// Call after registering any custom commands.
void carbon_instrument_start(void);
