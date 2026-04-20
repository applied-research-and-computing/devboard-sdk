/**
 * @file scpi_parser.c
 * @brief SCPI command parser and router
 */

#include "scpi_parser.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Forward declarations for command handlers
extern int scpi_handle_standard(const char *cmd, char *response, size_t max_len);
extern int scpi_handle_gpio(const char *cmd, char *response, size_t max_len);
extern int scpi_handle_adc(const char *cmd, char *response, size_t max_len);
extern int scpi_handle_uart(const char *cmd, char *response, size_t max_len);

// External init functions
extern void scpi_standard_init(void);
extern void scpi_gpio_init(void);
extern void scpi_adc_init(void);
extern void scpi_uart_init(void);

void scpi_init(void) {
    scpi_standard_init();
    scpi_gpio_init();
    scpi_adc_init();
    scpi_uart_init();
}

/**
 * Convert only the SCPI command mnemonic to uppercase, preserving arguments.
 */
static void str_toupper_command(char *str) {
    while (*str && !isspace((unsigned char)*str)) {
        *str = toupper((unsigned char)*str);
        str++;
    }
}

/**
 * Trim leading and trailing whitespace
 */
static char* str_trim(char *str) {
    // Trim leading
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // Trim trailing
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    
    return str;
}

int scpi_parse_command(const char *cmd_str, char *response_buf, size_t response_max_len) {
    if (!cmd_str || !response_buf || response_max_len == 0) {
        return 0;
    }
    
    // Copy command to mutable buffer and normalize
    char cmd_copy[256];
    strncpy(cmd_copy, cmd_str, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    char *cmd = str_trim(cmd_copy);
    str_toupper_command(cmd);
    
    // Empty command
    if (strlen(cmd) == 0) {
        return 0;
    }
    
    // Route to appropriate handler based on prefix
    if (cmd[0] == '*') {
        // Standard IEEE 488.2 commands
        return scpi_handle_standard(cmd, response_buf, response_max_len);
    } else if (strncmp(cmd, "GPIO:", 5) == 0) {
        return scpi_handle_gpio(cmd, response_buf, response_max_len);
    } else if (strncmp(cmd, "ADC:", 4) == 0) {
        return scpi_handle_adc(cmd, response_buf, response_max_len);
    } else if (strncmp(cmd, "UART:", 5) == 0) {
        return scpi_handle_uart(cmd, response_buf, response_max_len);
    } else {
        // Unknown command
        snprintf(response_buf, response_max_len, "ERROR: Unknown command");
        return strlen(response_buf);
    }
}
