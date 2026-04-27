#pragma once

#include <stddef.h>

int scpi_parse_command(const char *cmd_str, char *response_buf, size_t response_max_len);
