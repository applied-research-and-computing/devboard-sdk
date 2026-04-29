#include "carbon_error_queue.h"

#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "carbon_err_queue";

#ifdef CONFIG_CARBON_ERROR_QUEUE_SIZE
#define QUEUE_SIZE CONFIG_CARBON_ERROR_QUEUE_SIZE
#else
#define QUEUE_SIZE 16
#endif

#define MSG_LEN 64

typedef struct {
    int  code;
    char message[MSG_LEN];
} err_entry_t;

static err_entry_t       s_queue[QUEUE_SIZE];
static int               s_head  = 0;  // next entry to pop (oldest)
static int               s_tail  = 0;  // next slot to write
static int               s_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

void carbon_error_queue_init(void)
{
    if (s_mutex != NULL) return;
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create error queue mutex");
        return;
    }
    s_head  = 0;
    s_tail  = 0;
    s_count = 0;
    ESP_LOGI(TAG, "Error queue initialised (size %d)", QUEUE_SIZE);
}

void carbon_push_error(int code, const char *message)
{
    if (s_mutex == NULL) return;
    if (message == NULL) message = "";

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_count == QUEUE_SIZE) {
        s_head = (s_head + 1) % QUEUE_SIZE;
        s_count--;
        ESP_LOGW(TAG, "Error queue full — oldest entry discarded");
    }

    s_queue[s_tail].code = code;
    strncpy(s_queue[s_tail].message, message, MSG_LEN - 1);
    s_queue[s_tail].message[MSG_LEN - 1] = '\0';
    s_tail  = (s_tail + 1) % QUEUE_SIZE;
    s_count++;

    xSemaphoreGive(s_mutex);
}

int carbon_pop_error(char *buf, size_t buf_len)
{
    if (s_mutex == NULL || buf == NULL || buf_len == 0) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int ret;
    if (s_count == 0) {
        ret = snprintf(buf, buf_len, "0,\"No error\"");
    } else {
        int  code = s_queue[s_head].code;
        char msg[MSG_LEN];
        strncpy(msg, s_queue[s_head].message, MSG_LEN - 1);
        msg[MSG_LEN - 1] = '\0';
        s_head  = (s_head + 1) % QUEUE_SIZE;
        s_count--;
        ret = snprintf(buf, buf_len, "%d,\"%s\"", code, msg);
    }

    xSemaphoreGive(s_mutex);

    if (ret < 0) {
        buf[0] = '\0';
        return 0;
    }
    return ret < (int)buf_len ? ret : (int)buf_len - 1;
}

int carbon_error_count(void)
{
    if (s_mutex == NULL) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_count;
    xSemaphoreGive(s_mutex);
    return n;
}

void carbon_clear_errors(void)
{
    if (s_mutex == NULL) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_head  = 0;
    s_tail  = 0;
    s_count = 0;
    xSemaphoreGive(s_mutex);
}
