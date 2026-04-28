#include "scpi_watchdog.h"

#include "freertos/FreeRTOS.h"
#if configUSE_TIMERS != 1
#error "scpi_watchdog requires FreeRTOS software timers (set configUSE_TIMERS=1)"
#endif
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "scpi_watchdog";

#define DEFAULT_TIMEOUT_MS 5000
#define DONE_BIT           (1UL << 0)
#define TIMEOUT_BIT        (1UL << 1)

typedef struct {
    const carbon_cmd_descriptor_t *desc;
    const char                    *cmd;
    char                          *response_buf;
    size_t                         response_max_len;
    volatile int                   result;
    TaskHandle_t                   caller_task;
    TaskHandle_t                   handler_task;
} watchdog_ctx_t;

static void handler_task_fn(void *arg)
{
    watchdog_ctx_t *ctx = arg;
    ctx->result = ctx->desc->handler(ctx->cmd, ctx->response_buf, ctx->response_max_len);
    xTaskNotify(ctx->caller_task, DONE_BIT, eSetBits);
    vTaskSuspend(NULL);
}

/* Timer callback runs in the timer daemon task — blocking I/O is unsafe here.
   Only send the notification; the caller logs after waking. */
static void watchdog_cb(TimerHandle_t timer)
{
    watchdog_ctx_t *ctx = pvTimerGetTimerID(timer);
    xTaskNotify(ctx->caller_task, TIMEOUT_BIT, eSetBits);
}

int scpi_watchdog_dispatch(const carbon_cmd_descriptor_t *desc,
                           const char *cmd,
                           char *response_buf,
                           size_t response_max_len)
{
    int timeout_ms = desc->timeout_ms > 0 ? desc->timeout_ms : DEFAULT_TIMEOUT_MS;

    watchdog_ctx_t *ctx = malloc(sizeof(watchdog_ctx_t));
    if (!ctx) {
        return desc->handler(cmd, response_buf, response_max_len);
    }
    ctx->desc            = desc;
    ctx->cmd             = cmd;
    ctx->response_buf    = response_buf;
    ctx->response_max_len = response_max_len;
    ctx->result          = 0;
    ctx->caller_task     = xTaskGetCurrentTaskHandle();
    ctx->handler_task    = NULL;

    TimerHandle_t timer = xTimerCreate("cmd_wdg", pdMS_TO_TICKS(timeout_ms),
                                       pdFALSE, ctx, watchdog_cb);
    if (!timer) {
        free(ctx);
        return desc->handler(cmd, response_buf, response_max_len);
    }

    if (xTaskCreate(handler_task_fn, "cmd_handler",
                    CONFIG_CARBON_HANDLER_TASK_STACK_SIZE,
                    ctx, uxTaskPriorityGet(NULL), &ctx->handler_task) != pdPASS) {
        xTimerDelete(timer, 0);
        free(ctx);
        return desc->handler(cmd, response_buf, response_max_len);
    }

    xTimerStart(timer, 0);

    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);

    xTimerStop(timer, portMAX_DELAY);
    xTimerDelete(timer, portMAX_DELAY);

    /* Suspend before delete: prevents a race where the handler task resumes
       between the notify and the delete call. Safe to call on an already-
       suspended task (becomes a no-op in FreeRTOS). */
    vTaskSuspend(ctx->handler_task);
    vTaskDelete(ctx->handler_task);

    int ret;
    if (bits & DONE_BIT) {
        ret = ctx->result;
    } else {
        ESP_LOGW(TAG, "Command '%s' timed out after %d ms",
                 desc->scpi_command, timeout_ms);
        ret = snprintf(response_buf, response_max_len, "-365,\"Time out error\"");
        if (ret < 0) ret = 0;
    }

    free(ctx);
    return ret;
}
