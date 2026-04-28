#pragma once

#include "carbon_instrument.h"
#include <stddef.h>

/*
 * Dispatches a SCPI command handler with a FreeRTOS software timer watchdog.
 *
 * The handler runs in a dedicated task. A one-shot timer fires after
 * desc->timeout_ms (default 5000 ms if <= 0). If the handler finishes first,
 * the timer is cancelled and the result is returned normally. If the timer
 * fires first, the handler task is suspended and deleted, an error response
 * is written to response_buf, and the caller continues normally.
 *
 * CONSTRAINT: Handlers dispatched through this function MUST NOT hold
 * FreeRTOS mutexes or semaphores across blocking calls. If a handler is
 * killed by the watchdog while holding a mutex, that mutex is never released
 * and subsequent commands using the same peripheral will deadlock.
 */
int scpi_watchdog_dispatch(const carbon_cmd_descriptor_t *desc,
                           const char *cmd,
                           char *response_buf,
                           size_t response_max_len);
