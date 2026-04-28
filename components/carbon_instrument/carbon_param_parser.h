#pragma once

#include "carbon_instrument.h"
#include <stddef.h>

// Parse parameters from the tail of a command string (everything after "CMD_NAME ").
// Each descriptor in descs[0..desc_count-1] drives type conversion and range validation.
// Parsed values are written to out[0..desc_count-1].
// STRING values are copied into str_scratch; str_val pointers remain valid as long as
// str_scratch is live (i.e. for the duration of the handler call).
// On parse or validation error, writes a human-readable message to response_buf and returns < 0.
// On success, returns the number of parameters parsed (== desc_count).
int carbon_parse_params(const char *cmd_tail,
                        const carbon_param_t *descs,
                        int desc_count,
                        carbon_parsed_param_t *out,
                        char *str_scratch,
                        size_t scratch_len,
                        char *response_buf,
                        size_t response_max);
