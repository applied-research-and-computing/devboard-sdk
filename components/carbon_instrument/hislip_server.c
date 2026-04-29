#include "hislip_server.h"
#include "hislip.h"
#include "scpi_parser.h"
#include "sdkconfig.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "hislip_server";

#ifndef CONFIG_HISLIP_SYNC_PORT
#define HISLIP_SYNC_PORT 4880
#else
#define HISLIP_SYNC_PORT CONFIG_HISLIP_SYNC_PORT
#endif

#ifndef CONFIG_HISLIP_WORKER_QUEUE_DEPTH
#define HISLIP_WORKER_QUEUE_DEPTH 8
#else
#define HISLIP_WORKER_QUEUE_DEPTH CONFIG_HISLIP_WORKER_QUEUE_DEPTH
#endif

#define HISLIP_LISTEN_BACKLOG         2
#define HISLIP_ACCEPT_TASK_STACK_SIZE 4096
#define HISLIP_CLIENT_TASK_STACK_SIZE 6144
#define HISLIP_TASK_PRIORITY          (tskIDLE_PRIORITY + 5)
#define SCPI_RESPONSE_BUF_SIZE        8192
#define SOCKET_TIMEOUT_SEC            10
#define DEVICE_CLEAR_BIT              (1UL << 2)
#define PROC_DONE_BIT                 (1UL << 3)
#define WORKER_DONE_BIT               (1UL << 4)

typedef struct {
    uint16_t          session_id;
    int               sync_sock;          // set for the lifetime of sync_loop
    int               async_sock;         // -1 until async channel connects
    TaskHandle_t      sync_task;
    TaskHandle_t      async_task;
    TaskHandle_t      proc_task;          // worker task: always-on command executor
    bool              sync_open;
    bool              async_open;
    SemaphoreHandle_t lock;               // protects status_byte, async_sock, open flags
    SemaphoreHandle_t async_send_lock;    // serialises writes to async_sock
    SemaphoreHandle_t sync_send_lock;     // serialises writes to sync_sock
    SemaphoreHandle_t clear_done;         // sync signals async after completing device clear
    uint8_t           status_byte;        // IEEE 488.2 status byte (MAV = bit 4)
    bool              remote_mode;
    bool              overlap_mode;
    volatile bool     device_clear_pending;
    QueueHandle_t     cmd_queue;          // command queue: always active
} hislip_session_t;

typedef struct {
    uint32_t     message_id;
    char        *cmd;           // heap-allocated; NULL + !is_trigger = shutdown sentinel
    size_t       len;
    bool         is_trigger;    // true for TRIGGER messages; cmd is unused
    TaskHandle_t notify_task;   // non-NULL in sync mode: worker notifies server on completion
} hislip_pending_cmd_t;

static hislip_session_t s_session = {
    .session_id           = 1,
    .sync_sock            = -1,
    .async_sock           = -1,
    .sync_task            = NULL,
    .async_task           = NULL,
    .proc_task            = NULL,
    .sync_open            = false,
    .async_open           = false,
    .lock                 = NULL,
    .async_send_lock      = NULL,
    .sync_send_lock       = NULL,
    .clear_done           = NULL,
    .status_byte          = 0,
    .remote_mode          = false,
    .overlap_mode         = false,
    .device_clear_pending = false,
    .cmd_queue            = NULL,
};

static void set_socket_timeouts(int sock)
{
    struct timeval timeout = {
        .tv_sec  = SOCKET_TIMEOUT_SEC,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static void session_lock(void)
{
    xSemaphoreTake(s_session.lock, portMAX_DELAY);
}

static void session_unlock(void)
{
    xSemaphoreGive(s_session.lock);
}

static int send_error(int sock, uint8_t code)
{
    return hislip_send_message(sock, HISLIP_MSG_ERROR, code, 0, NULL, 0);
}

// Send a fatal error on sock. Caller must close the socket afterwards.
static int send_fatal_error(int sock, uint8_t code)
{
    ESP_LOGE(TAG, "Fatal HiSLIP error %u on socket %d", code, sock);
    return hislip_send_message(sock, HISLIP_MSG_FATAL_ERROR, code, 0, NULL, 0);
}

// Serialised write to the async socket. All tasks sending on async_sock must use
// this to prevent message interleaving between async_loop responses and SRQ emissions.
static int async_send(int sock, uint8_t msg_type, uint8_t cc, uint32_t param,
                      const uint8_t *payload, uint64_t len)
{
    xSemaphoreTake(s_session.async_send_lock, portMAX_DELAY);
    int ret = hislip_send_message(sock, msg_type, cc, param, payload, len);
    xSemaphoreGive(s_session.async_send_lock);
    return ret;
}

// Serialised write to the sync socket. Used by the worker task and the server task
// fast-path (overlap mode) to prevent interleaving, and by async_loop for
// DEVICE_CLEAR_COMPLETE which shares the sync channel.
static int sync_send(int sock, uint8_t msg_type, uint8_t cc, uint32_t param,
                     const uint8_t *payload, uint64_t len)
{
    xSemaphoreTake(s_session.sync_send_lock, portMAX_DELAY);
    int ret = hislip_send_message(sock, msg_type, cc, param, payload, len);
    xSemaphoreGive(s_session.sync_send_lock);
    return ret;
}

// Emit ASYNC_SERVICE_REQUEST to notify the client that status has changed.
// No-op if the async channel is not yet open.
static void emit_srq(uint8_t stb)
{
    session_lock();
    int sock = s_session.async_sock;
    session_unlock();
    if (sock < 0) return;
    async_send(sock, HISLIP_MSG_ASYNC_SERVICE_REQUEST, stb, 0, NULL, 0);
}

static int handle_initialize(int sock, uint8_t client_cc, uint32_t msg_param,
                             const uint8_t *payload, size_t payload_len)
{
    uint16_t client_version = (uint16_t)((msg_param >> 16) & 0xFFFF);
    uint16_t negotiated     = client_version == 0 || client_version > 0x0100 ? 0x0100 : client_version;
    uint16_t session_id;
    bool     overlap        = (client_cc & 1) != 0;

    s_session.cmd_queue = xQueueCreate(HISLIP_WORKER_QUEUE_DEPTH, sizeof(hislip_pending_cmd_t));
    if (s_session.cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create worker queue");
        send_fatal_error(sock, HISLIP_ERROR_UNIDENTIFIED);
        return -1;
    }

    session_lock();
    session_id             = s_session.session_id;
    s_session.sync_open    = true;
    s_session.status_byte  = 0;
    s_session.overlap_mode = overlap;
    session_unlock();

    if (payload_len > 0) {
        ESP_LOGI(TAG, "HiSLIP sub-address: %.*s", (int)payload_len, (const char *)payload);
    }

    uint32_t response_param = ((uint32_t)negotiated << 16) | session_id;
    ESP_LOGI(TAG, "Initialize: client=0x%04x negotiated=0x%04x session=%u mode=%s",
             client_version, negotiated, session_id, overlap ? "overlap" : "synchronized");

    return hislip_send_message(sock, HISLIP_MSG_INITIALIZE_RESPONSE, overlap ? 1 : 0,
                               response_param, NULL, 0);
}

static int append_payload(uint8_t *buffer, size_t *buffer_len, const uint8_t *payload, size_t payload_len)
{
    if (*buffer_len + payload_len > HISLIP_MAX_PAYLOAD_SIZE) {
        return -1;
    }

    memcpy(buffer + *buffer_len, payload, payload_len);
    *buffer_len += payload_len;
    return 0;
}

extern void carbon_fire_trigger(void);

// Commands handled inline on the server task, bypassing the worker queue.
// All have timeout_ms <= 500 and perform only register reads or queue pops —
// no hardware I/O. *WAI is excluded: its purpose is to synchronise with
// pending operations, so it must be ordered behind queued commands.
static bool is_fast_command(const char *cmd)
{
    // Strip trailing whitespace/newline — HiSLIP payloads include the "\n" the
    // client appends, so compare only the command text itself.
    const char *end = cmd + strlen(cmd);
    while (end > cmd && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ')) {
        --end;
    }
    size_t len = (size_t)(end - cmd);

    static const char *const fast[] = {
        "*IDN?", "*CLS", "*ESR?", "*ESE?", "*ESE",
        "*STB?", "*OPC?", "SYST:ERR?", "SYST:ERR:COUN?",
    };
    for (size_t i = 0; i < sizeof(fast) / sizeof(fast[0]); i++) {
        if (strlen(fast[i]) == len && strncasecmp(cmd, fast[i], len) == 0) return true;
    }
    return false;
}

static void command_processor_task(void *arg)
{
    char *response = malloc(SCPI_RESPONSE_BUF_SIZE);
    if (response == NULL) {
        ESP_LOGE(TAG, "Processor: failed to allocate response buffer");
        xTaskNotify(s_session.sync_task, PROC_DONE_BIT, eSetBits);
        vTaskDelete(NULL);
        return;
    }

    hislip_pending_cmd_t pending;
    while (xQueueReceive(s_session.cmd_queue, &pending, portMAX_DELAY) == pdTRUE) {
        // Shutdown sentinel: cmd=NULL, is_trigger=false
        if (!pending.is_trigger && pending.cmd == NULL) {
            break;
        }

        // Normal SCPI command
        ESP_LOGI(TAG, "SCPI command (worker): %s", pending.cmd);
        int response_len = scpi_parse_command(pending.cmd, response, SCPI_RESPONSE_BUF_SIZE);
        free(pending.cmd);

        if (s_session.device_clear_pending) {
            hislip_pending_cmd_t drain;
            while (xQueueReceive(s_session.cmd_queue, &drain, 0) == pdTRUE) {
                free(drain.cmd);  // free(NULL) is safe for trigger items
            }
            xTaskNotifyStateClear(NULL);  // discard stale DEVICE_CLEAR_BIT
            continue;                     // sync_loop handles *CLS via DEVICE_CLEAR_ACK
            // Note: notify_task not signalled here; async_loop already woke sync_task
            // via DEVICE_CLEAR_BIT before this continue path is reached.
        }

        if (response_len < 0) {
            snprintf(response, SCPI_RESPONSE_BUF_SIZE, "ERROR: Invalid command");
            response_len = strlen(response);
        }

        if (response_len > 0) {
            sync_send(s_session.sync_sock, HISLIP_MSG_DATA_END, 0, pending.message_id,
                      (const uint8_t *)response, (uint64_t)response_len);
            uint8_t stb;
            session_lock();
            s_session.status_byte |= 0x10;  // set MAV
            stb = s_session.status_byte;
            session_unlock();
            emit_srq(stb);
        }
        if (pending.notify_task) {
            xTaskNotify(pending.notify_task, WORKER_DONE_BIT, eSetBits);
        }
    }

    free(response);
    xTaskNotify(s_session.sync_task, PROC_DONE_BIT, eSetBits);
    vTaskDelete(NULL);
}

static void sync_loop(int sock, uint8_t *rx_buffer)
{
    s_session.sync_task = xTaskGetCurrentTaskHandle();
    s_session.sync_sock = sock;

    uint8_t *command     = calloc(1, HISLIP_MAX_PAYLOAD_SIZE + 1);
    char    *response    = calloc(1, SCPI_RESPONSE_BUF_SIZE);
    size_t   command_len = 0;

    if (command == NULL || response == NULL) {
        ESP_LOGE(TAG, "Failed to allocate sync buffers");
        free(command);
        free(response);
        send_fatal_error(sock, HISLIP_ERROR_UNIDENTIFIED);
        s_session.sync_task = NULL;
        s_session.sync_sock = -1;
        return;
    }

    {
        BaseType_t ok = xTaskCreate(command_processor_task, "hislip_worker",
                                    HISLIP_CLIENT_TASK_STACK_SIZE, NULL,
                                    HISLIP_TASK_PRIORITY, &s_session.proc_task);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create worker task, falling back to inline mode");
            s_session.proc_task = NULL;
        }
    }

    while (1) {
        uint8_t  msg_type;
        uint8_t  control_code;
        uint32_t msg_param;
        size_t   payload_len;

        if (hislip_recv_message(sock, &msg_type, &control_code, &msg_param,
                                rx_buffer, HISLIP_MAX_PAYLOAD_SIZE, &payload_len) < 0) {
            break;
        }

        if (msg_type == HISLIP_MSG_DATA || msg_type == HISLIP_MSG_DATA_END) {
            if (append_payload(command, &command_len, rx_buffer, payload_len) < 0) {
                ESP_LOGE(TAG, "HiSLIP command exceeded %d bytes", HISLIP_MAX_PAYLOAD_SIZE);
                send_error(sock, HISLIP_ERROR_POORLY_FORMED_MSG);
                command_len = 0;
                continue;
            }

            if (msg_type == HISLIP_MSG_DATA) {
                continue;
            }

            command[command_len] = '\0';

            if (s_session.proc_task != NULL && !is_fast_command((char *)command)) {
                // Slow path: enqueue for worker task execution.
                char *cmd_copy = malloc(command_len + 1);
                if (cmd_copy == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate command copy");
                    send_error(sock, HISLIP_ERROR_UNIDENTIFIED);
                } else {
                    memcpy(cmd_copy, command, command_len + 1);
                    hislip_pending_cmd_t pending = {
                        .message_id  = msg_param,
                        .cmd         = cmd_copy,
                        .len         = command_len,
                        .notify_task = s_session.overlap_mode ? NULL : xTaskGetCurrentTaskHandle(),
                    };
                    TickType_t q_timeout = s_session.overlap_mode
                                          ? pdMS_TO_TICKS(1000)
                                          : portMAX_DELAY;
                    if (xQueueSend(s_session.cmd_queue, &pending, q_timeout) != pdTRUE) {
                        ESP_LOGW(TAG, "Command queue full, dropping command");
                        free(cmd_copy);
                        // Respond with a SCPI error payload on the normal DATA_END channel
                        // so the client sees a command response rather than a protocol error.
                        // Using msg_param preserves the message_id for overlap-mode matching.
                        static const char queue_full[] = "-350,\"Queue overflow\"";
                        sync_send(sock, HISLIP_MSG_DATA_END, 0, msg_param,
                                  (const uint8_t *)queue_full, sizeof(queue_full) - 1);
                    } else if (!s_session.overlap_mode) {
                        // Sync mode: block until worker sends response or device clear interrupts.
                        xTaskNotifyWait(0, WORKER_DONE_BIT | DEVICE_CLEAR_BIT, NULL, portMAX_DELAY);
                    }
                }
            } else {
                // Fast path: handle inline on the server task. Also used as fallback
                // if the worker task failed to start (proc_task == NULL).
                ESP_LOGI(TAG, "SCPI command (inline): %s", (char *)command);
                int response_len = scpi_parse_command((const char *)command, response,
                                                      SCPI_RESPONSE_BUF_SIZE);

                if (!s_session.device_clear_pending) {
                    if (response_len < 0) {
                        snprintf(response, SCPI_RESPONSE_BUF_SIZE, "ERROR: Invalid command");
                        response_len = strlen(response);
                    }

                    if (response_len > 0) {
                        if (sync_send(sock, HISLIP_MSG_DATA_END, 0, msg_param,
                                      (const uint8_t *)response,
                                      (uint64_t)response_len) < 0) {
                            break;
                        }
                        uint8_t stb;
                        session_lock();
                        s_session.status_byte |= 0x10;  // set MAV
                        stb = s_session.status_byte;
                        session_unlock();
                        emit_srq(stb);
                    }
                }
            }
            command_len = 0;
        } else if (msg_type == HISLIP_MSG_TRIGGER) {
            // Triggers are always handled inline for lowest latency.
            carbon_fire_trigger();
            sync_send(sock, HISLIP_MSG_DATA_END, 0, msg_param, NULL, 0);
        } else if (msg_type == HISLIP_MSG_DEVICE_CLEAR_ACK) {
            xTaskNotifyStateClear(NULL);  // discard stale DEVICE_CLEAR_BIT and WORKER_DONE_BIT
            command_len = 0;
            scpi_parse_command("*CLS", response, SCPI_RESPONSE_BUF_SIZE);
            s_session.device_clear_pending = false;
            xSemaphoreGive(s_session.clear_done);
        } else {
            bool async_only = msg_type == HISLIP_MSG_ASYNC_LOCK
                           || msg_type == HISLIP_MSG_ASYNC_LOCK_RESPONSE
                           || msg_type == HISLIP_MSG_ASYNC_REMOTE_LOCAL_CTRL
                           || msg_type == HISLIP_MSG_ASYNC_REMOTE_LOCAL_RESP
                           || msg_type == HISLIP_MSG_ASYNC_MAX_MSG_SIZE
                           || msg_type == HISLIP_MSG_ASYNC_MAX_MSG_SIZE_RESP
                           || msg_type >= HISLIP_MSG_ASYNC_INITIALIZE;
            if (async_only) {
                ESP_LOGW(TAG, "Async-only message type %u on sync channel", msg_type);
                send_error(sock, HISLIP_ERROR_ATTEMPT_TO_USE_CONN);
            } else {
                ESP_LOGW(TAG, "Unhandled sync message type: %u", msg_type);
                send_error(sock, HISLIP_ERROR_UNIDENTIFIED);
            }
        }
    }

    if (s_session.proc_task != NULL) {
        hislip_pending_cmd_t pending;
        while (xQueueReceive(s_session.cmd_queue, &pending, 0) == pdTRUE) {
            free(pending.cmd);
        }
        hislip_pending_cmd_t sentinel = {.cmd = NULL, .message_id = 0, .len = 0};
        xQueueSend(s_session.cmd_queue, &sentinel, portMAX_DELAY);
        uint32_t bits;
        xTaskNotifyWait(0, PROC_DONE_BIT, &bits, pdMS_TO_TICKS(2000));
        s_session.proc_task = NULL;
    }

    free(command);
    free(response);
    s_session.sync_task = NULL;
    s_session.sync_sock = -1;
}

static int handle_async_initialize(int sock, uint32_t msg_param)
{
    uint16_t requested_session = (uint16_t)(msg_param & 0xFFFF);
    bool valid;

    session_lock();
    valid = s_session.sync_open && !s_session.async_open
            && requested_session == s_session.session_id;
    if (valid) {
        s_session.async_open = true;
        s_session.async_sock = sock;
    }
    session_unlock();

    if (!valid) {
        ESP_LOGE(TAG, "Invalid async initialize for session %u", requested_session);
        return send_error(sock, HISLIP_ERROR_INVALID_INIT_SEQ);
    }

    ESP_LOGI(TAG, "AsyncInitialize: session=%u", requested_session);
    return async_send(sock, HISLIP_MSG_ASYNC_INITIALIZE_RESP, 0, 0x00004342, NULL, 0);
}

static void async_loop(int sock, uint8_t *rx_buffer)
{
    s_session.async_task = xTaskGetCurrentTaskHandle();

    while (1) {
        uint8_t  msg_type;
        uint8_t  control_code;
        uint32_t msg_param;
        size_t   payload_len;

        if (hislip_recv_message(sock, &msg_type, &control_code, &msg_param,
                                rx_buffer, HISLIP_MAX_PAYLOAD_SIZE, &payload_len) < 0) {
            break;
        }

        switch (msg_type) {
            case HISLIP_MSG_ASYNC_MAX_MSG_SIZE: {
                uint64_t proposed = HISLIP_MAX_PAYLOAD_SIZE;
                if (payload_len >= sizeof(uint64_t)) {
                    uint64_t encoded;
                    memcpy(&encoded, rx_buffer, sizeof(encoded));
                    proposed = hislip_ntohll(encoded);
                }
                uint64_t negotiated       = proposed < HISLIP_MAX_PAYLOAD_SIZE ? proposed : HISLIP_MAX_PAYLOAD_SIZE;
                uint64_t encoded_response = hislip_htonll(negotiated);
                async_send(sock, HISLIP_MSG_ASYNC_MAX_MSG_SIZE_RESP, 0, 0,
                           (const uint8_t *)&encoded_response, sizeof(encoded_response));
                break;
            }

            case HISLIP_MSG_ASYNC_LOCK:
                async_send(sock, HISLIP_MSG_ASYNC_LOCK_RESPONSE, 1, 0, NULL, 0);
                break;

            case HISLIP_MSG_ASYNC_STATUS_QUERY: {
                uint8_t stb;
                session_lock();
                stb = s_session.status_byte;
                if (control_code & 1) {
                    s_session.status_byte &= ~0x10;  // clear MAV
                }
                session_unlock();
                async_send(sock, HISLIP_MSG_ASYNC_STATUS_RESPONSE, stb, 0, NULL, 0);
                break;
            }

            case HISLIP_MSG_ASYNC_DEVICE_CLEAR: {
                s_session.device_clear_pending = true;

                // Notify both the worker task (to abort the current handler via watchdog
                // timeout) and the sync task (to unblock it if waiting on WORKER_DONE_BIT).
                if (s_session.proc_task != NULL) {
                    xTaskNotify(s_session.proc_task, DEVICE_CLEAR_BIT, eSetBits);
                }
                if (s_session.sync_task != NULL) {
                    xTaskNotify(s_session.sync_task, DEVICE_CLEAR_BIT, eSetBits);
                }

                if (s_session.sync_sock >= 0) {
                    sync_send(s_session.sync_sock,
                              HISLIP_MSG_DEVICE_CLEAR_COMPLETE, 0, 0, NULL, 0);
                }

                if (xSemaphoreTake(s_session.clear_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
                    ESP_LOGW(TAG, "Device clear timed out");
                    s_session.device_clear_pending = false;
                }

                async_send(sock, HISLIP_MSG_ASYNC_DEV_CLEAR_ACK, 0, 0, NULL, 0);
                break;
            }

            case HISLIP_MSG_ASYNC_REMOTE_LOCAL_CTRL: {
                session_lock();
                s_session.remote_mode = (control_code > 0);
                session_unlock();
                ESP_LOGI(TAG, "Remote mode: %s", control_code > 0 ? "enabled" : "disabled");
                async_send(sock, HISLIP_MSG_ASYNC_REMOTE_LOCAL_RESP, control_code, 0, NULL, 0);
                break;
            }

            default: {
                bool sync_only = msg_type == HISLIP_MSG_DATA
                              || msg_type == HISLIP_MSG_DATA_END
                              || msg_type == HISLIP_MSG_DEVICE_CLEAR_COMPLETE
                              || msg_type == HISLIP_MSG_DEVICE_CLEAR_ACK
                              || msg_type == HISLIP_MSG_TRIGGER
                              || msg_type == HISLIP_MSG_INTERRUPTED;
                if (sync_only) {
                    ESP_LOGW(TAG, "Sync-only message type %u on async channel", msg_type);
                    async_send(sock, HISLIP_MSG_ERROR, HISLIP_ERROR_ATTEMPT_TO_USE_CONN, 0, NULL, 0);
                } else {
                    ESP_LOGW(TAG, "Unhandled async message type: %u", msg_type);
                    async_send(sock, HISLIP_MSG_ERROR, HISLIP_ERROR_UNIDENTIFIED, 0, NULL, 0);
                }
                break;
            }
        }
    }

    session_lock();
    s_session.async_sock = -1;
    s_session.async_open = false;
    session_unlock();
    s_session.async_task = NULL;

    ESP_LOGI(TAG, "Async channel closed for session %u", s_session.session_id);
}

static void client_task(void *arg)
{
    int      sock      = (int)(intptr_t)arg;
    uint8_t *rx_buffer = malloc(HISLIP_MAX_PAYLOAD_SIZE);

    if (rx_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate receive buffer");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    set_socket_timeouts(sock);

    uint8_t  msg_type;
    uint8_t  control_code;
    uint32_t msg_param;
    size_t   payload_len;

    if (hislip_recv_message(sock, &msg_type, &control_code, &msg_param,
                            rx_buffer, HISLIP_MAX_PAYLOAD_SIZE, &payload_len) == 0) {
        if (msg_type == HISLIP_MSG_INITIALIZE) {
            if (handle_initialize(sock, control_code, msg_param, rx_buffer, payload_len) == 0) {
                sync_loop(sock, rx_buffer);
            }
            session_lock();
            s_session.sync_open    = false;
            s_session.status_byte  = 0;
            s_session.overlap_mode = false;
            if (s_session.cmd_queue != NULL) {
                vQueueDelete(s_session.cmd_queue);
                s_session.cmd_queue = NULL;
            }
            uint16_t next         = s_session.session_id + 1;
            s_session.session_id  = (next == 0) ? 1 : next;
            session_unlock();
        } else if (msg_type == HISLIP_MSG_ASYNC_INITIALIZE) {
            if (handle_async_initialize(sock, msg_param) == 0) {
                async_loop(sock, rx_buffer);
            }
        } else {
            ESP_LOGW(TAG, "Unexpected first HiSLIP message type: %u", msg_type);
            send_error(sock, HISLIP_ERROR_INVALID_INIT_SEQ);
        }
    }

    free(rx_buffer);
    close(sock);
    vTaskDelete(NULL);
}

static void accept_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(HISLIP_SYNC_PORT),
    };

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, HISLIP_LISTEN_BACKLOG) < 0) {
        ESP_LOGE(TAG, "listen() failed: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "HiSLIP listening on port %d", HISLIP_SYNC_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "accept() failed: errno %d", errno);
            continue;
        }

        char addr_buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_buf, sizeof(addr_buf));
        ESP_LOGI(TAG, "Accepted HiSLIP connection from %s:%u", addr_buf, ntohs(client_addr.sin_port));

        BaseType_t ok = xTaskCreate(client_task, "hislip_client", HISLIP_CLIENT_TASK_STACK_SIZE,
                                    (void *)(intptr_t)client_sock, HISLIP_TASK_PRIORITY, NULL);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client task");
            close(client_sock);
        }
    }
}

extern void scpi_standard_set_status_source(uint8_t *ptr);

void hislip_server_start(void)
{
    scpi_standard_set_status_source(&s_session.status_byte);

    if (s_session.lock == NULL) {
        s_session.lock = xSemaphoreCreateMutex();
        if (s_session.lock == NULL) {
            ESP_LOGE(TAG, "Failed to create session mutex");
            return;
        }
    }

    if (s_session.async_send_lock == NULL) {
        s_session.async_send_lock = xSemaphoreCreateMutex();
        if (s_session.async_send_lock == NULL) {
            ESP_LOGE(TAG, "Failed to create async send mutex");
            return;
        }
    }

    if (s_session.sync_send_lock == NULL) {
        s_session.sync_send_lock = xSemaphoreCreateMutex();
        if (s_session.sync_send_lock == NULL) {
            ESP_LOGE(TAG, "Failed to create sync send mutex");
            return;
        }
    }

    if (s_session.clear_done == NULL) {
        s_session.clear_done = xSemaphoreCreateBinary();
        if (s_session.clear_done == NULL) {
            ESP_LOGE(TAG, "Failed to create clear semaphore");
            return;
        }
    }

    BaseType_t ok = xTaskCreate(accept_task, "hislip_accept", HISLIP_ACCEPT_TASK_STACK_SIZE,
                                NULL, HISLIP_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create accept task");
    }
}
