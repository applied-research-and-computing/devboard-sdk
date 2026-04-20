/**
 * @file scpi_standard.c
 * @brief IEEE 488.2 standard SCPI commands
 */

#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "scpi_std";

// Device identity
#define MANUFACTURER "CARBON"
#define MODEL "ESP32-INSTRUMENT"
#define FIRMWARE_VERSION "v1.0.0"

#ifndef CONFIG_DEVICE_SERIAL
#define DEVICE_SERIAL "SN12345"
#else
#define DEVICE_SERIAL CONFIG_DEVICE_SERIAL
#endif

void scpi_standard_init(void) {
    ESP_LOGI(TAG, "Standard SCPI commands initialized");
}

int scpi_handle_standard(const char *cmd, char *response, size_t max_len) {
    // *IDN? - Identification query
    if (strcmp(cmd, "*IDN?") == 0) {
        snprintf(response, max_len, MANUFACTURER "," MODEL "," DEVICE_SERIAL "," FIRMWARE_VERSION);
        return strlen(response);
    }
    
    // *RST - Reset
    else if (strcmp(cmd, "*RST") == 0) {
        ESP_LOGI(TAG, "Device reset requested");
        // TODO: Reset GPIO outputs, ADC config, UART state
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    // *CLS - Clear status
    else if (strcmp(cmd, "*CLS") == 0) {
        ESP_LOGI(TAG, "Status cleared");
        // TODO: Clear error queue
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    // *ESR? - Event status register query
    else if (strcmp(cmd, "*ESR?") == 0) {
        snprintf(response, max_len, "0");
        return strlen(response);
    }

    // *ESE / *ESE? - Event status enable register
    else if (strcmp(cmd, "*ESE?") == 0) {
        snprintf(response, max_len, "0");
        return strlen(response);
    } else if (strncmp(cmd, "*ESE ", 5) == 0) {
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    // *OPC? - Operation complete query
    else if (strcmp(cmd, "*OPC?") == 0) {
        snprintf(response, max_len, "1");
        return strlen(response);
    }

    // *TST? - Self-test query
    else if (strcmp(cmd, "*TST?") == 0) {
        snprintf(response, max_len, "0");
        return strlen(response);
    }

    // *WAI - Wait to continue
    else if (strcmp(cmd, "*WAI") == 0) {
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    // Unknown standard command
    else {
        snprintf(response, max_len, "ERROR: Unknown standard command");
        return strlen(response);
    }
}
