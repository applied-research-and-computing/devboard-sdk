#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Typed response helpers for Carbon instrument handlers.
//
// Each helper writes a daemon-parseable string into resp[0..n-1] and returns
// strlen(resp) — matching the handler return convention.  All helpers
// null-terminate the buffer even on truncation.
//
// Recommended minimum buffer size: 256 bytes.  The response buffer passed to
// handlers by the SDK is always at least that large.
//
// IMPORTANT: Do NOT include physical units in the response string.  Units
// belong in the instrument profile's response.unit field, not here.

// Formats a double as a bare decimal string (e.g. "3.3") parseable as TypedValue float_value.
int carbon_respond_float(char *resp, size_t n, double value);

// Formats a 64-bit integer (e.g. "42") parseable as TypedValue int_value.
int carbon_respond_int(char *resp, size_t n, int64_t value);

// Formats a boolean as "1" or "0" parseable as TypedValue bool_value.
// carbond accepts "1"/"ON" for true and "0"/"OFF" for false — not "true"/"false".
int carbon_respond_bool(char *resp, size_t n, bool value);

// Copies value verbatim; parseable as TypedValue enum_value.  NULL value treated as "".
int carbon_respond_enum(char *resp, size_t n, const char *value);

// Formats values as comma-separated decimals (e.g. "1.0,2.5,3.0") parseable as
// TypedValue float_array using carbond's default "," delimiter.
// On buffer overflow, stops after the last fully written element.
int carbon_respond_float_array(char *resp, size_t n, const double *values, int count);

// Formats a structured error: "ERR:<code>:<message>".
// Errors surface in CommandResponse.error and are not parsed as a TypedValue.
// NULL message treated as "".  On truncation, the buffer is null-terminated.
int carbon_respond_error(char *resp, size_t n, int code, const char *message);
