/**
 * @file scpi_uart.c
 * @brief UART passthrough SCPI commands
 */

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "scpi_uart";

#define UART_NUM UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define UART_BUF_SIZE 1024

static bool uart_initialized = false;

void scpi_uart_init(void) {
    if (!uart_initialized) {
        // Default UART configuration: 115200 8N1
        uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        
        esp_err_t err = uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
            return;
        }

        err = uart_param_config(UART_NUM, &uart_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART parameter configuration failed: %s", esp_err_to_name(err));
            uart_driver_delete(UART_NUM);
            return;
        }

        err = uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART pin configuration failed: %s", esp_err_to_name(err));
            uart_driver_delete(UART_NUM);
            return;
        }
        
        uart_initialized = true;
        ESP_LOGI(TAG, "UART%d initialized (TX=GPIO%d, RX=GPIO%d, 115200 8N1)",
                 UART_NUM, UART_TX_PIN, UART_RX_PIN);
    }
}

int scpi_handle_uart(const char *cmd, char *response, size_t max_len) {
    if (!uart_initialized) {
        snprintf(response, max_len, "ERROR: UART not initialized");
        return strlen(response);
    }

    // UART:WRITE <data>
    if (strncmp(cmd, "UART:WRITE ", 11) == 0) {
        const char *data = cmd + 11;
        int len = strlen(data);
        
        if (len == 0) {
            snprintf(response, max_len, "ERROR: No data to write");
            return strlen(response);
        }
        
        int written = uart_write_bytes(UART_NUM, data, len);
        
        if (written < 0) {
            snprintf(response, max_len, "ERROR: UART write failed");
            return strlen(response);
        }
        
        ESP_LOGI(TAG, "Wrote %d bytes to UART", written);
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    // UART:READ?
    else if (strcmp(cmd, "UART:READ?") == 0) {
        uint8_t buf[256];
        int len = uart_read_bytes(UART_NUM, buf, sizeof(buf) - 1, 100 / portTICK_PERIOD_MS);
        
        if (len < 0) {
            snprintf(response, max_len, "ERROR: UART read failed");
            return strlen(response);
        }
        
        if (len == 0) {
            if (max_len > 0) {
                response[0] = '\0';
            }
            return 0;
        }
        
        // Null-terminate and copy to response
        buf[len] = '\0';
        
        // Check if data is printable ASCII, otherwise hex-encode
        bool is_printable = true;
        for (int i = 0; i < len; i++) {
            if (buf[i] < 32 || buf[i] > 126) {
                is_printable = false;
                break;
            }
        }
        
        if (is_printable) {
            snprintf(response, max_len, "%s", (char*)buf);
        } else {
            // Hex-encode binary data
            int offset = 0;
            for (int i = 0; i < len && offset < max_len - 3; i++) {
                offset += snprintf(response + offset, max_len - offset, "%02X", buf[i]);
            }
        }
        
        ESP_LOGI(TAG, "Read %d bytes from UART", len);
        return strlen(response);
    }
    
    // UART:CONFIG <baud> <data_bits> <parity> <stop_bits>
    else if (strncmp(cmd, "UART:CONFIG ", 12) == 0) {
        int baud, data_bits, stop_bits;
        char parity_str[8];
        
        if (sscanf(cmd + 12, "%d %d %7s %d", &baud, &data_bits, parity_str, &stop_bits) != 4) {
            snprintf(response, max_len, "ERROR: Invalid UART:CONFIG syntax");
            return strlen(response);
        }
        
        // Validate and convert parameters
        uart_word_length_t data_bits_enum;
        switch (data_bits) {
            case 5: data_bits_enum = UART_DATA_5_BITS; break;
            case 6: data_bits_enum = UART_DATA_6_BITS; break;
            case 7: data_bits_enum = UART_DATA_7_BITS; break;
            case 8: data_bits_enum = UART_DATA_8_BITS; break;
            default:
                snprintf(response, max_len, "ERROR: Data bits must be 5-8");
                return strlen(response);
        }
        
        uart_parity_t parity_enum;
        if (strcasecmp(parity_str, "NONE") == 0) {
            parity_enum = UART_PARITY_DISABLE;
        } else if (strcasecmp(parity_str, "EVEN") == 0) {
            parity_enum = UART_PARITY_EVEN;
        } else if (strcasecmp(parity_str, "ODD") == 0) {
            parity_enum = UART_PARITY_ODD;
        } else {
            snprintf(response, max_len, "ERROR: Parity must be NONE, EVEN, or ODD");
            return strlen(response);
        }
        
        uart_stop_bits_t stop_bits_enum;
        if (stop_bits == 1) {
            stop_bits_enum = UART_STOP_BITS_1;
        } else if (stop_bits == 2) {
            stop_bits_enum = UART_STOP_BITS_2;
        } else {
            snprintf(response, max_len, "ERROR: Stop bits must be 1 or 2");
            return strlen(response);
        }
        
        // Apply configuration
        esp_err_t err = uart_set_baudrate(UART_NUM, baud);
        if (err != ESP_OK) {
            snprintf(response, max_len, "ERROR: UART baud config failed");
            return strlen(response);
        }

        err = uart_set_word_length(UART_NUM, data_bits_enum);
        if (err != ESP_OK) {
            snprintf(response, max_len, "ERROR: UART data bits config failed");
            return strlen(response);
        }

        err = uart_set_parity(UART_NUM, parity_enum);
        if (err != ESP_OK) {
            snprintf(response, max_len, "ERROR: UART parity config failed");
            return strlen(response);
        }

        err = uart_set_stop_bits(UART_NUM, stop_bits_enum);
        if (err != ESP_OK) {
            snprintf(response, max_len, "ERROR: UART stop bits config failed");
            return strlen(response);
        }
        
        ESP_LOGI(TAG, "UART configured: %d %d%s%d", baud, data_bits, parity_str, stop_bits);
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    else {
        snprintf(response, max_len, "ERROR: Unknown UART command");
        return strlen(response);
    }
}
