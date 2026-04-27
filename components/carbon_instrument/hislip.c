#include "hislip.h"
#include <string.h>
#include <errno.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "hislip";

uint64_t hislip_htonll(uint64_t value) {
    // Check if system is little-endian
    uint32_t test = 1;
    if (*(uint8_t *)&test == 1) {
        // Little-endian: swap bytes
        uint32_t high = htonl((uint32_t)(value >> 32));
        uint32_t low = htonl((uint32_t)(value & 0xFFFFFFFF));
        return ((uint64_t)low << 32) | high;
    }
    return value;  // Big-endian: no swap needed
}

uint64_t hislip_ntohll(uint64_t value) {
    return hislip_htonll(value);
}

void hislip_pack_header(uint8_t msg_type, uint8_t control_code, uint32_t msg_param,
                        uint64_t payload_len, uint8_t *buf) {
    hislip_header_t *header = (hislip_header_t *)buf;
    
    // Set prologue
    header->prologue[0] = 'H';
    header->prologue[1] = 'S';
    
    // Set message type and control code
    header->msg_type = msg_type;
    header->control_code = control_code;
    
    // Set message parameter and payload length (network byte order)
    header->msg_param = htonl(msg_param);
    header->payload_len = hislip_htonll(payload_len);
}

int hislip_recv_message(int sock, uint8_t *msg_type_out, uint8_t *control_code_out,
                        uint32_t *msg_param_out, uint8_t *payload_out, size_t max_payload,
                        size_t *payload_len_out) {
    uint8_t header_buf[HISLIP_HEADER_SIZE];
    size_t total_received = 0;
    
    // Receive header (16 bytes) - loop until complete
    while (total_received < HISLIP_HEADER_SIZE) {
        int n = recv(sock, header_buf + total_received, 
                     HISLIP_HEADER_SIZE - total_received, 0);
        if (n <= 0) {
            if (n == 0) {
                ESP_LOGW(TAG, "Connection closed while receiving header");
                return -1;
            }
            if (errno == EINTR) continue;
            ESP_LOGE(TAG, "recv() failed: errno %d", errno);
            return -1;
        }
        total_received += n;
    }
    
    // Parse header
    hislip_header_t *header = (hislip_header_t *)header_buf;
    
    // Validate prologue
    if (header->prologue[0] != 'H' || header->prologue[1] != 'S') {
        ESP_LOGE(TAG, "Invalid HiSLIP prologue: %c%c", 
                 header->prologue[0], header->prologue[1]);
        return -1;
    }
    
    // Extract fields
    *msg_type_out = header->msg_type;
    *control_code_out = header->control_code;
    *msg_param_out = ntohl(header->msg_param);
    uint64_t payload_len = hislip_ntohll(header->payload_len);
    
    ESP_LOGI(TAG, "Received header: type=%d, ctrl=%d, param=%lu, payload_len=%llu",
             *msg_type_out, *control_code_out, *msg_param_out, payload_len);
    
    // Validate payload length
    if (payload_len > max_payload) {
        ESP_LOGE(TAG, "Payload too large: %llu > %zu", payload_len, max_payload);
        return -1;
    }
    
    *payload_len_out = (size_t)payload_len;
    
    // Receive payload if present
    if (payload_len > 0) {
        total_received = 0;
        while (total_received < payload_len) {
            int n = recv(sock, payload_out + total_received,
                         payload_len - total_received, 0);
            if (n <= 0) {
                if (n == 0) {
                    ESP_LOGW(TAG, "Connection closed while receiving payload");
                    return -1;
                }
                if (errno == EINTR) continue;
                ESP_LOGE(TAG, "recv() payload failed: errno %d", errno);
                return -1;
            }
            total_received += n;
        }
    }
    
    return 0;
}

int hislip_send_message(int sock, uint8_t msg_type, uint8_t control_code,
                        uint32_t msg_param, const uint8_t *payload, uint64_t payload_len) {
    uint8_t header_buf[HISLIP_HEADER_SIZE];
    
    // Pack header
    hislip_pack_header(msg_type, control_code, msg_param, payload_len, header_buf);
    
    ESP_LOGI(TAG, "Sending message: type=%d, ctrl=%d, param=%lu, payload_len=%llu",
             msg_type, control_code, msg_param, payload_len);
    
    // Send header
    size_t total_sent = 0;
    while (total_sent < HISLIP_HEADER_SIZE) {
        int n = send(sock, header_buf + total_sent,
                     HISLIP_HEADER_SIZE - total_sent, 0);
        if (n <= 0) {
            if (errno == EINTR) continue;
            ESP_LOGE(TAG, "send() header failed: errno %d", errno);
            return -1;
        }
        total_sent += n;
    }
    
    // Send payload if present
    if (payload_len > 0 && payload != NULL) {
        total_sent = 0;
        while (total_sent < payload_len) {
            int n = send(sock, payload + total_sent,
                         payload_len - total_sent, 0);
            if (n <= 0) {
                if (errno == EINTR) continue;
                ESP_LOGE(TAG, "send() payload failed: errno %d", errno);
                return -1;
            }
            total_sent += n;
        }
    }
    
    return 0;
}
