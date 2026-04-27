#include "scpi_parser.h"
#include "carbon_registry.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void str_toupper_command(char *str)
{
    while (*str && !isspace((unsigned char)*str)) {
        *str = toupper((unsigned char)*str);
        str++;
    }
}

static char *str_trim(char *str)
{
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

int scpi_parse_command(const char *cmd_str, char *response_buf, size_t response_max_len)
{
    if (!cmd_str || !response_buf || response_max_len == 0) return 0;

    char cmd_copy[256];
    strncpy(cmd_copy, cmd_str, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char *cmd = str_trim(cmd_copy);
    str_toupper_command(cmd);

    if (strlen(cmd) == 0) return 0;

    int count = carbon_registry_count();
    for (int i = 0; i < count; i++) {
        const carbon_cmd_descriptor_t *desc = carbon_registry_get(i);
        size_t scpi_len = strlen(desc->scpi_command);
        if (strcmp(cmd, desc->scpi_command) == 0 ||
            (strncmp(cmd, desc->scpi_command, scpi_len) == 0 && cmd[scpi_len] == ' '))
        {
            return desc->handler(cmd, response_buf, response_max_len);
        }
    }

    snprintf(response_buf, response_max_len, "ERROR: Unknown command");
    return strlen(response_buf);
}
