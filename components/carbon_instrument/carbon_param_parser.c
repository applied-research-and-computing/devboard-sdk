#include "carbon_param_parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

/* Advance past spaces, tabs, and commas. */
static void skip_delimiters(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == ',') (*p)++;
}

/* Copy the next space/comma-delimited token into buf[buflen].
   Advances *p past the token (but not past the following delimiter).
   Returns false if the string is empty after skipping leading delimiters. */
static bool next_token(const char **p, char *buf, size_t buflen)
{
    skip_delimiters(p);
    if (**p == '\0') return false;
    size_t i = 0;
    while (**p && **p != ' ' && **p != '\t' && **p != ',') {
        if (i < buflen - 1) buf[i++] = **p;
        (*p)++;
    }
    buf[i] = '\0';
    return true;
}

/* Copy the remainder of p into buf[buflen], stripping trailing whitespace.
   Used for a STRING param that is the last (or only) parameter, so the full
   remaining text (spaces included) is the value. */
static void rest_trimmed(const char *p, char *buf, size_t buflen)
{
    while (*p == ' ' || *p == '\t') p++;
    size_t len = strlen(p);
    while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t' ||
                        p[len-1] == '\r' || p[len-1] == '\n')) {
        len--;
    }
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
}

int carbon_parse_params(const char *cmd_tail,
                        const carbon_param_t *descs,
                        int desc_count,
                        carbon_parsed_param_t *out,
                        char *str_scratch,
                        size_t scratch_len,
                        char *response_buf,
                        size_t response_max)
{
    if (desc_count == 0) return 0;
    if (!cmd_tail) cmd_tail = "";

    const char *pos = cmd_tail;
    size_t scratch_used = 0;

    for (int i = 0; i < desc_count; i++) {
        const carbon_param_t *d = &descs[i];
        char token[256];
        bool have_token;

        /* A STRING that is the last parameter consumes the rest of the line so
           spaces within the value are preserved. */
        if (d->type == CARBON_PARAM_STRING && i == desc_count - 1) {
            rest_trimmed(pos, token, sizeof(token));
            have_token = (token[0] != '\0');
        } else {
            have_token = next_token(&pos, token, sizeof(token));
        }

        if (!have_token || token[0] == '\0') {
            if (d->default_value) {
                strncpy(token, d->default_value, sizeof(token) - 1);
                token[sizeof(token) - 1] = '\0';
            } else {
                snprintf(response_buf, response_max,
                         "ERR:1:missing required parameter '%s'", d->name);
                return -1;
            }
        }

        out[i].name = d->name;
        out[i].type = d->type;

        switch (d->type) {
            case CARBON_PARAM_INT: {
                char *end;
                long v = strtol(token, &end, 10);
                if (end == token || *end != '\0') {
                    snprintf(response_buf, response_max,
                             "ERR:2:parameter '%s': expected integer, got '%s'",
                             d->name, token);
                    return -1;
                }
                if (d->max > d->min && ((double)v < d->min || (double)v > d->max)) {
                    snprintf(response_buf, response_max,
                             "ERR:3:parameter '%s': %ld out of range [%.0f, %.0f]",
                             d->name, v, d->min, d->max);
                    return -1;
                }
                out[i].int_val = (int)v;
                break;
            }
            case CARBON_PARAM_FLOAT: {
                char *end;
                float v = strtof(token, &end);
                if (end == token || *end != '\0') {
                    snprintf(response_buf, response_max,
                             "ERR:2:parameter '%s': expected float, got '%s'",
                             d->name, token);
                    return -1;
                }
                if (d->max > d->min && ((double)v < d->min || (double)v > d->max)) {
                    snprintf(response_buf, response_max,
                             "ERR:3:parameter '%s': %g out of range [%g, %g]",
                             d->name, (double)v, d->min, d->max);
                    return -1;
                }
                out[i].float_val = v;
                break;
            }
            case CARBON_PARAM_BOOL: {
                bool bv;
                if (strcasecmp(token, "1") == 0 || strcasecmp(token, "true") == 0 ||
                    strcasecmp(token, "on") == 0  || strcasecmp(token, "yes") == 0) {
                    bv = true;
                } else if (strcasecmp(token, "0") == 0 || strcasecmp(token, "false") == 0 ||
                           strcasecmp(token, "off") == 0 || strcasecmp(token, "no") == 0) {
                    bv = false;
                } else {
                    snprintf(response_buf, response_max,
                             "ERR:2:parameter '%s': expected boolean (0/1/true/false/on/off),"
                             " got '%s'", d->name, token);
                    return -1;
                }
                out[i].bool_val = bv;
                break;
            }
            case CARBON_PARAM_STRING: {
                size_t len = strlen(token) + 1;
                if (scratch_used + len > scratch_len) {
                    snprintf(response_buf, response_max,
                             "ERR:2:parameter '%s': string too long", d->name);
                    return -1;
                }
                memcpy(str_scratch + scratch_used, token, len);
                out[i].str_val = str_scratch + scratch_used;
                scratch_used += len;
                break;
            }
            case CARBON_PARAM_ENUM: {
                int matched = -1;
                for (int j = 0; j < d->enum_count; j++) {
                    if (strcasecmp(token, d->enum_values[j]) == 0) {
                        matched = j;
                        break;
                    }
                }
                if (matched < 0) {
                    /* Build a readable list of valid choices for the error message. */
                    char valid[128] = "";
                    for (int j = 0; j < d->enum_count; j++) {
                        if (j > 0)
                            strncat(valid, "|", sizeof(valid) - strlen(valid) - 1);
                        strncat(valid, d->enum_values[j], sizeof(valid) - strlen(valid) - 1);
                    }
                    snprintf(response_buf, response_max,
                             "ERR:2:parameter '%s': expected %s, got '%s'",
                             d->name, valid, token);
                    return -1;
                }
                out[i].int_val = matched;
                break;
            }
        }
    }
    return desc_count;
}
