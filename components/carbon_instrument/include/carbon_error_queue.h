#pragma once

#include <stddef.h>

// Standard SCPI error codes (IEEE 488.2 §21.8)
#define CARBON_ERR_UNDEFINED_COMMAND   (-100)
#define CARBON_ERR_INVALID_PARAM       (-200)
#define CARBON_ERR_PARAM_OUT_OF_RANGE  (-222)
#define CARBON_ERR_HARDWARE            (-300)

// Must be called once from carbon_instrument_start() before any handler can run.
// Idempotent — safe to call more than once.
void carbon_error_queue_init(void);

// Push an error onto the queue. Thread-safe. If the queue is full the oldest
// entry is silently overwritten. message may not be NULL.
void carbon_push_error(int code, const char *message);

// Pop the oldest error and format the SCPI wire response into buf:
//   error present → "-200,\"Invalid parameter\""
//   queue empty   → "0,\"No error\""
// Returns bytes written (excluding NUL), like snprintf.
int carbon_pop_error(char *buf, size_t n);

// Return current number of entries in the queue.
int carbon_error_count(void);

// Discard all queued errors. Called by *CLS.
void carbon_clear_errors(void);
