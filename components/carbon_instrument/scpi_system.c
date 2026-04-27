#include "carbon_instrument.h"
#include "carbon_registry.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "scpi_system";

static const char *param_type_str(carbon_param_type_t t)
{
    switch (t) {
        case CARBON_PARAM_INT:    return "int";
        case CARBON_PARAM_FLOAT:  return "float";
        case CARBON_PARAM_STRING: return "string";
        case CARBON_PARAM_BOOL:   return "bool";
        case CARBON_PARAM_ENUM:   return "enum";
        default:                  return "unknown";
    }
}

// Lightweight JSON write helpers — write into buf[pos], return -1 if full.
static int jw_raw(char *buf, size_t *pos, size_t max, const char *s)
{
    size_t len = strlen(s);
    if (*pos + len >= max) return -1;
    memcpy(buf + *pos, s, len);
    *pos += len;
    return 0;
}

static int jw_str(char *buf, size_t *pos, size_t max, const char *val)
{
    if (!val) return jw_raw(buf, pos, max, "null");
    if (*pos >= max - 2) return -1;
    buf[(*pos)++] = '"';
    for (const char *c = val; *c; c++) {
        if (*pos >= max - 3) return -1;
        if (*c == '"' || *c == '\\') buf[(*pos)++] = '\\';
        buf[(*pos)++] = *c;
    }
    buf[(*pos)++] = '"';
    return 0;
}

static int jw_dbl(char *buf, size_t *pos, size_t max, double val)
{
    char tmp[32];
    if (val == (long long)val) snprintf(tmp, sizeof(tmp), "%lld", (long long)val);
    else                       snprintf(tmp, sizeof(tmp), "%g", val);
    return jw_raw(buf, pos, max, tmp);
}

#define JW(s)      do { if (jw_raw(buf, &pos, n, s)    < 0) goto trunc; } while (0)
#define JW_STR(s)  do { if (jw_str(buf, &pos, n, s)    < 0) goto trunc; } while (0)
#define JW_INT(v)  do { if (jw_dbl(buf, &pos, n, (double)(v)) < 0) goto trunc; } while (0)
#define JW_DBL(v)  do { if (jw_dbl(buf, &pos, n, v)    < 0) goto trunc; } while (0)

static int commands_handler(const char *cmd, char *r, size_t n)
{
    const carbon_instrument_config_t *cfg = carbon_instrument_get_config();
    char *buf = r;
    size_t pos = 0;

    JW("{\"identity\":{");
    JW("\"manufacturer\":"); JW_STR(cfg->manufacturer); JW(",");
    JW("\"model\":");        JW_STR(cfg->model);        JW(",");
    JW("\"serial\":");       JW_STR(cfg->serial);       JW(",");
    JW("\"firmware\":");     JW_STR(cfg->firmware_version);
    JW("},\"commands\":[");

    bool first_cmd = true;
    int count = carbon_registry_count();
    for (int i = 0; i < count; i++) {
        const carbon_cmd_descriptor_t *desc = carbon_registry_get(i);
        if (strncmp(desc->scpi_command, "SYSTEM:", 7) == 0) continue;

        if (!first_cmd) JW(",");
        first_cmd = false;

        JW("{\"scpi\":"); JW_STR(desc->scpi_command);
        JW(",\"type\":"); JW_STR(desc->type == CARBON_CMD_QUERY ? "query" : "write");
        if (desc->group)       { JW(",\"group\":");       JW_STR(desc->group); }
        if (desc->description) { JW(",\"description\":"); JW_STR(desc->description); }
        JW(",\"timeout_ms\":"); JW_INT(desc->timeout_ms);
        JW(",\"params\":[");

        for (int j = 0; j < desc->param_count; j++) {
            const carbon_param_t *p = &desc->params[j];
            if (j > 0) JW(",");
            JW("{");
            if (p->name) { JW("\"name\":"); JW_STR(p->name); JW(","); }
            JW("\"type\":"); JW_STR(param_type_str(p->type));
            if (p->description) { JW(",\"description\":"); JW_STR(p->description); }
            if ((p->type == CARBON_PARAM_INT || p->type == CARBON_PARAM_FLOAT) && p->max > p->min) {
                JW(",\"min\":"); JW_DBL(p->min);
                JW(",\"max\":"); JW_DBL(p->max);
            }
            if (p->type == CARBON_PARAM_ENUM && p->enum_count > 0) {
                JW(",\"values\":[");
                for (int k = 0; k < p->enum_count; k++) {
                    if (k > 0) JW(",");
                    JW_STR(p->enum_values[k]);
                }
                JW("]");
            }
            if (p->default_value) { JW(",\"default\":"); JW_STR(p->default_value); }
            JW("}");
        }
        JW("]}");
    }

    JW("]}");
    buf[pos] = '\0';
    ESP_LOGI(TAG, "SYSTEM:COMMANDS? sent %zu bytes", pos);
    return (int)pos;

trunc:
    buf[pos < n ? pos : n - 1] = '\0';
    ESP_LOGW(TAG, "SYSTEM:COMMANDS? truncated at %zu bytes (increase SCPI_RESPONSE_BUF_SIZE)", pos);
    return (int)(pos < n ? pos : n - 1);
}

void scpi_system_init(void)
{
    static const carbon_cmd_descriptor_t cmd = {
        .scpi_command = "SYSTEM:COMMANDS?",
        .type         = CARBON_CMD_QUERY,
        .group        = "System",
        .description  = "Return all registered commands and device identity as JSON",
        .param_count  = 0,
        .timeout_ms   = 2000,
        .handler      = commands_handler,
    };
    carbon_register_command(&cmd);
    ESP_LOGI(TAG, "System commands registered");
}
