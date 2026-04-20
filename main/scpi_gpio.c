/**
 * @file scpi_gpio.c
 * @brief GPIO control SCPI commands
 */

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "scpi_gpio";

void scpi_gpio_init(void) {
    // GPIO subsystem initialized on-demand per pin
    ESP_LOGI(TAG, "GPIO SCPI commands initialized");
}

/**
 * Validate GPIO pin number
 */
static bool is_valid_pin(int pin) {
    switch (pin) {
        case 2:
        case 4:
        case 5:
        case 13:
        case 14:
        case 18:
        case 19:
        case 21:
        case 22:
        case 23:
        case 25:
        case 26:
        case 27:
        case 32:
        case 33:
            return true;
        default:
            return false;
    }
}

int scpi_handle_gpio(const char *cmd, char *response, size_t max_len) {
    // GPIO:SET <pin> <value>
    if (strncmp(cmd, "GPIO:SET ", 9) == 0) {
        int pin, value;
        if (sscanf(cmd + 9, "%d %d", &pin, &value) != 2) {
            snprintf(response, max_len, "ERROR: Invalid GPIO:SET syntax");
            return strlen(response);
        }
        
        if (!is_valid_pin(pin)) {
            snprintf(response, max_len, "ERROR: Invalid pin %d", pin);
            return strlen(response);
        }
        
        if (value != 0 && value != 1) {
            snprintf(response, max_len, "ERROR: Value must be 0 or 1");
            return strlen(response);
        }
        
        // Configure as output if not already
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, value);
        
        ESP_LOGI(TAG, "GPIO%d set to %d", pin, value);
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    // GPIO:GET? <pin>
    else if (strncmp(cmd, "GPIO:GET? ", 10) == 0) {
        int pin;
        if (sscanf(cmd + 10, "%d", &pin) != 1) {
            snprintf(response, max_len, "ERROR: Invalid GPIO:GET? syntax");
            return strlen(response);
        }
        
        if (!is_valid_pin(pin)) {
            snprintf(response, max_len, "ERROR: Invalid pin %d", pin);
            return strlen(response);
        }
        
        int level = gpio_get_level(pin);
        snprintf(response, max_len, "%d", level);
        return strlen(response);
    }
    
    // GPIO:CONFIG <pin> <INPUT|OUTPUT>
    else if (strncmp(cmd, "GPIO:CONFIG ", 12) == 0) {
        int pin;
        char mode_str[16];
        if (sscanf(cmd + 12, "%d %15s", &pin, mode_str) != 2) {
            snprintf(response, max_len, "ERROR: Invalid GPIO:CONFIG syntax");
            return strlen(response);
        }
        
        if (!is_valid_pin(pin)) {
            snprintf(response, max_len, "ERROR: Invalid pin %d", pin);
            return strlen(response);
        }
        
        gpio_mode_t mode;
        if (strcasecmp(mode_str, "INPUT") == 0) {
            mode = GPIO_MODE_INPUT;
        } else if (strcasecmp(mode_str, "OUTPUT") == 0) {
            mode = GPIO_MODE_OUTPUT;
        } else {
            snprintf(response, max_len, "ERROR: Mode must be INPUT or OUTPUT");
            return strlen(response);
        }
        
        gpio_set_direction(pin, mode);
        
        ESP_LOGI(TAG, "GPIO%d configured as %s", pin, mode_str);
        snprintf(response, max_len, "OK");
        return strlen(response);
    }
    
    else {
        snprintf(response, max_len, "ERROR: Unknown GPIO command");
        return strlen(response);
    }
}
