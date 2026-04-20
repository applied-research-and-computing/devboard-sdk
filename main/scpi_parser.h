/**
 * @file scpi_parser.h
 * @brief SCPI command parser interface
 */

#ifndef SCPI_PARSER_H
#define SCPI_PARSER_H

#include <stddef.h>
#include <stdint.h>

/**
 * Parse and execute a SCPI command
 * 
 * @param cmd_str Null-terminated SCPI command string
 * @param response_buf Buffer to write response into
 * @param response_max_len Maximum size of response buffer
 * @return Length of response written (0 if no response)
 */
int scpi_parse_command(const char *cmd_str, char *response_buf, size_t response_max_len);

/**
 * Initialize SCPI subsystem (GPIO, ADC, UART)
 */
void scpi_init(void);

#endif // SCPI_PARSER_H
