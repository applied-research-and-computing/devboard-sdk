#include "carbon_response.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

int carbon_respond_float(char *resp, size_t n, double value)
{
    snprintf(resp, n, "%.6g", value);
    return (int)strlen(resp);
}

int carbon_respond_int(char *resp, size_t n, int64_t value)
{
    snprintf(resp, n, "%" PRId64, value);
    return (int)strlen(resp);
}

int carbon_respond_bool(char *resp, size_t n, bool value)
{
    snprintf(resp, n, "%d", value ? 1 : 0);
    return (int)strlen(resp);
}

int carbon_respond_enum(char *resp, size_t n, const char *value)
{
    if (!value) value = "";
    snprintf(resp, n, "%s", value);
    return (int)strlen(resp);
}

int carbon_respond_float_array(char *resp, size_t n, const double *values, int count)
{
    if (n == 0) return 0;
    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        int written = snprintf(resp + pos, n - pos, "%s%.6g",
                               i > 0 ? "," : "", values[i]);
        if (written < 0 || (size_t)written >= n - pos) {
            resp[n - 1] = '\0';
            break;
        }
        pos += (size_t)written;
    }
    return (int)strlen(resp);
}

int carbon_respond_error(char *resp, size_t n, int code, const char *message)
{
    if (!message) message = "";
    int written = snprintf(resp, n, "ERR:%d:%s", code, message);
    if (written < 0 || (size_t)written >= n) {
        if (n > 0) resp[n - 1] = '\0';
    }
    return (int)strlen(resp);
}
