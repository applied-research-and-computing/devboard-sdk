#ifndef HISLIP_H
#define HISLIP_H

#include <stdint.h>
#include <stddef.h>

// HiSLIP protocol constants
#define HISLIP_PROLOGUE "HS"
#define HISLIP_HEADER_SIZE 16
#define HISLIP_MAX_PAYLOAD_SIZE (8 * 1024)

// HiSLIP message types
#define HISLIP_MSG_INITIALIZE               0
#define HISLIP_MSG_INITIALIZE_RESPONSE      1
#define HISLIP_MSG_FATAL_ERROR              2
#define HISLIP_MSG_ERROR                    3
#define HISLIP_MSG_ASYNC_LOCK               4
#define HISLIP_MSG_ASYNC_LOCK_RESPONSE      5
#define HISLIP_MSG_DATA                     6
#define HISLIP_MSG_DATA_END                 7
#define HISLIP_MSG_DEVICE_CLEAR_COMPLETE    8
#define HISLIP_MSG_DEVICE_CLEAR_ACK         9
#define HISLIP_MSG_ASYNC_REMOTE_LOCAL_CTRL 10
#define HISLIP_MSG_ASYNC_REMOTE_LOCAL_RESP 11
#define HISLIP_MSG_TRIGGER                  12
#define HISLIP_MSG_INTERRUPTED              13
#define HISLIP_MSG_ASYNC_INTERRUPTED        14
#define HISLIP_MSG_ASYNC_MAX_MSG_SIZE       15
#define HISLIP_MSG_ASYNC_MAX_MSG_SIZE_RESP  16
#define HISLIP_MSG_ASYNC_INITIALIZE         17
#define HISLIP_MSG_ASYNC_INITIALIZE_RESP    18
#define HISLIP_MSG_ASYNC_DEVICE_CLEAR       19
#define HISLIP_MSG_ASYNC_SERVICE_REQUEST    20
#define HISLIP_MSG_ASYNC_STATUS_QUERY       21
#define HISLIP_MSG_ASYNC_STATUS_RESPONSE    22
#define HISLIP_MSG_ASYNC_DEV_CLEAR_ACK      23
#define HISLIP_MSG_ASYNC_LOCK_INFO          24
#define HISLIP_MSG_ASYNC_LOCK_INFO_RESP     25
#define HISLIP_MSG_GET_DESCRIPTORS          26
#define HISLIP_MSG_GET_DESCRIPTORS_RESP     27
#define HISLIP_MSG_START_TLS                28
#define HISLIP_MSG_ASYNC_START_TLS          29
#define HISLIP_MSG_ASYNC_START_TLS_RESP     30
#define HISLIP_MSG_END_TLS                  31
#define HISLIP_MSG_ASYNC_END_TLS            32
#define HISLIP_MSG_ASYNC_END_TLS_RESP       33
#define HISLIP_MSG_GET_SASL_MECH_LIST       34
#define HISLIP_MSG_GET_SASL_MECH_LIST_RESP  35
#define HISLIP_MSG_AUTH_START               36
#define HISLIP_MSG_AUTH_START_RESP          37
#define HISLIP_MSG_AUTH_CONTINUE            38
#define HISLIP_MSG_AUTH_CONTINUE_RESP       39

// HiSLIP control codes
#define HISLIP_CONTROL_CODE_NONE            0

// HiSLIP error codes
#define HISLIP_ERROR_UNIDENTIFIED           0
#define HISLIP_ERROR_POORLY_FORMED_MSG      1
#define HISLIP_ERROR_ATTEMPT_TO_USE_CONN    2
#define HISLIP_ERROR_INVALID_INIT_SEQ       3
#define HISLIP_ERROR_SERVER_REFUSED         4

// HiSLIP message header structure (16 bytes, network byte order)
typedef struct __attribute__((packed)) {
    char prologue[2];        // "HS"
    uint8_t msg_type;        // Message type
    uint8_t control_code;    // Control code
    uint32_t msg_param;      // Message parameter (network byte order)
    uint64_t payload_len;    // Payload length (network byte order)
} hislip_header_t;

// Function prototypes
uint64_t hislip_htonll(uint64_t value);
uint64_t hislip_ntohll(uint64_t value);

void hislip_pack_header(uint8_t msg_type, uint8_t control_code, uint32_t msg_param, 
                        uint64_t payload_len, uint8_t *buf);

int hislip_recv_message(int sock, uint8_t *msg_type_out, uint8_t *control_code_out,
                        uint32_t *msg_param_out, uint8_t *payload_out, size_t max_payload,
                        size_t *payload_len_out);

int hislip_send_message(int sock, uint8_t msg_type, uint8_t control_code, 
                        uint32_t msg_param, const uint8_t *payload, uint64_t payload_len);

#endif // HISLIP_H
