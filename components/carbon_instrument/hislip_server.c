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

#define HISLIP_LISTEN_BACKLOG 2
#define HISLIP_ACCEPT_TASK_STACK_SIZE 4096
#define HISLIP_CLIENT_TASK_STACK_SIZE 6144
#define HISLIP_TASK_PRIORITY (tskIDLE_PRIORITY + 5)
#define SCPI_RESPONSE_BUF_SIZE 8192
#define SOCKET_TIMEOUT_SEC 10

typedef struct {
    uint16_t session_id;
    bool sync_open;
    bool async_open;
    bool mav;
    SemaphoreHandle_t lock;
} hislip_server_state_t;

static hislip_server_state_t s_state = {
    .session_id = 1,
    .sync_open = false,
    .async_open = false,
    .mav = false,
    .lock = NULL,
};

static void set_socket_timeouts(int sock)
{
    struct timeval timeout = {
        .tv_sec = SOCKET_TIMEOUT_SEC,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static void lock_state(void)
{
    if (s_state.lock != NULL) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
    }
}

static void unlock_state(void)
{
    if (s_state.lock != NULL) {
        xSemaphoreGive(s_state.lock);
    }
}

static int send_error(int sock, uint8_t code)
{
    return hislip_send_message(sock, HISLIP_MSG_ERROR, code, 0, NULL, 0);
}

static int handle_initialize(int sock, uint32_t msg_param, const uint8_t *payload, size_t payload_len)
{
    uint16_t client_version = (uint16_t)((msg_param >> 16) & 0xFFFF);
    uint16_t negotiated = client_version == 0 || client_version > 0x0100 ? 0x0100 : client_version;
    uint16_t session_id;

    lock_state();
    session_id = s_state.session_id;
    s_state.sync_open = true;
    s_state.mav = false;
    unlock_state();

    if (payload_len > 0) {
        ESP_LOGI(TAG, "HiSLIP sub-address: %.*s", (int)payload_len, (const char *)payload);
    }

    uint32_t response_param = ((uint32_t)negotiated << 16) | session_id;
    ESP_LOGI(TAG, "Initialize: client=0x%04x negotiated=0x%04x session=%u",
             client_version, negotiated, session_id);

    return hislip_send_message(sock, HISLIP_MSG_INITIALIZE_RESPONSE, 0, response_param, NULL, 0);
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

static void sync_loop(int sock, uint8_t *rx_buffer)
{
    uint8_t *command = calloc(1, HISLIP_MAX_PAYLOAD_SIZE + 1);
    char *response = calloc(1, SCPI_RESPONSE_BUF_SIZE);
    size_t command_len = 0;

    if (command == NULL || response == NULL) {
        ESP_LOGE(TAG, "Failed to allocate sync buffers");
        free(command);
        free(response);
        return;
    }

    while (1) {
        uint8_t msg_type;
        uint8_t control_code;
        uint32_t msg_param;
        size_t payload_len;

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
            ESP_LOGI(TAG, "SCPI command: %s", (char *)command);

            int response_len = scpi_parse_command((const char *)command, response, SCPI_RESPONSE_BUF_SIZE);
            command_len = 0;

            if (response_len < 0) {
                snprintf(response, SCPI_RESPONSE_BUF_SIZE, "ERROR: Invalid command");
                response_len = strlen(response);
            }

            if (response_len > 0) {
                if (hislip_send_message(sock, HISLIP_MSG_DATA_END, 0, msg_param,
                                        (const uint8_t *)response, (uint64_t)response_len) < 0) {
                    break;
                }

                lock_state();
                s_state.mav = true;
                unlock_state();
            }
        } else if (msg_type == HISLIP_MSG_TRIGGER) {
            ESP_LOGI(TAG, "Trigger received");
        } else if (msg_type == HISLIP_MSG_DEVICE_CLEAR_COMPLETE) {
            command_len = 0;
            scpi_parse_command("*CLS", response, SCPI_RESPONSE_BUF_SIZE);
            hislip_send_message(sock, HISLIP_MSG_DEVICE_CLEAR_ACK, 0, 0, NULL, 0);
        } else {
            ESP_LOGW(TAG, "Unhandled sync message type: %u", msg_type);
            send_error(sock, HISLIP_ERROR_UNIDENTIFIED);
        }
    }

    free(command);
    free(response);
}

static int handle_async_initialize(int sock, uint32_t msg_param)
{
    uint16_t requested_session = (uint16_t)(msg_param & 0xFFFF);
    bool valid;

    lock_state();
    valid = s_state.sync_open && requested_session == s_state.session_id;
    if (valid) {
        s_state.async_open = true;
    }
    unlock_state();

    if (!valid) {
        ESP_LOGE(TAG, "Invalid async initialize for session %u", requested_session);
        return send_error(sock, HISLIP_ERROR_INVALID_INIT_SEQ);
    }

    ESP_LOGI(TAG, "AsyncInitialize: session=%u", requested_session);
    return hislip_send_message(sock, HISLIP_MSG_ASYNC_INITIALIZE_RESP, 0, 0x00004342, NULL, 0);
}

static void async_loop(int sock, uint8_t *rx_buffer)
{
    while (1) {
        uint8_t msg_type;
        uint8_t control_code;
        uint32_t msg_param;
        size_t payload_len;

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

                uint64_t negotiated = proposed < HISLIP_MAX_PAYLOAD_SIZE ? proposed : HISLIP_MAX_PAYLOAD_SIZE;
                uint64_t encoded_response = hislip_htonll(negotiated);
                hislip_send_message(sock, HISLIP_MSG_ASYNC_MAX_MSG_SIZE_RESP, 0, 0,
                                    (const uint8_t *)&encoded_response, sizeof(encoded_response));
                break;
            }

            case HISLIP_MSG_ASYNC_LOCK:
                hislip_send_message(sock, HISLIP_MSG_ASYNC_LOCK_RESPONSE, 1, 0, NULL, 0);
                break;

            case HISLIP_MSG_ASYNC_STATUS_QUERY: {
                bool mav;
                lock_state();
                mav = s_state.mav;
                if (control_code & 1) {
                    s_state.mav = false;
                }
                unlock_state();

                hislip_send_message(sock, HISLIP_MSG_ASYNC_STATUS_RESPONSE, mav ? 0x10 : 0, 0, NULL, 0);
                break;
            }

            case HISLIP_MSG_ASYNC_DEVICE_CLEAR:
                hislip_send_message(sock, HISLIP_MSG_ASYNC_DEV_CLEAR_ACK, 0, 0, NULL, 0);
                break;

            case HISLIP_MSG_ASYNC_REMOTE_LOCAL_CTRL:
                hislip_send_message(sock, HISLIP_MSG_ASYNC_REMOTE_LOCAL_RESP, 0, 0, NULL, 0);
                break;

            default:
                ESP_LOGW(TAG, "Unhandled async message type: %u", msg_type);
                send_error(sock, HISLIP_ERROR_UNIDENTIFIED);
                break;
        }
    }
}

static void client_task(void *arg)
{
    int sock = (int)(intptr_t)arg;
    uint8_t *rx_buffer = malloc(HISLIP_MAX_PAYLOAD_SIZE);

    if (rx_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate receive buffer");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    set_socket_timeouts(sock);

    uint8_t msg_type;
    uint8_t control_code;
    uint32_t msg_param;
    size_t payload_len;

    if (hislip_recv_message(sock, &msg_type, &control_code, &msg_param,
                            rx_buffer, HISLIP_MAX_PAYLOAD_SIZE, &payload_len) == 0) {
        if (msg_type == HISLIP_MSG_INITIALIZE) {
            if (handle_initialize(sock, msg_param, rx_buffer, payload_len) == 0) {
                sync_loop(sock, rx_buffer);
            }
            lock_state();
            s_state.sync_open = false;
            s_state.async_open = false;
            s_state.mav = false;
            unlock_state();
        } else if (msg_type == HISLIP_MSG_ASYNC_INITIALIZE) {
            if (handle_async_initialize(sock, msg_param) == 0) {
                async_loop(sock, rx_buffer);
            }
            lock_state();
            s_state.async_open = false;
            unlock_state();
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
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(HISLIP_SYNC_PORT),
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
        socklen_t client_len = sizeof(client_addr);
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

void hislip_server_start(void)
{
    if (s_state.lock == NULL) {
        s_state.lock = xSemaphoreCreateMutex();
        if (s_state.lock == NULL) {
            ESP_LOGE(TAG, "Failed to create session mutex");
            return;
        }
    }

    BaseType_t ok = xTaskCreate(accept_task, "hislip_accept", HISLIP_ACCEPT_TASK_STACK_SIZE,
                                NULL, HISLIP_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create accept task");
    }
}
