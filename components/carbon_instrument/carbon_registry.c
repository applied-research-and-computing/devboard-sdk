#include "carbon_registry.h"
#include "carbon_instrument.h"
#include "esp_log.h"

#ifdef CONFIG_CARBON_MAX_COMMANDS
#define CARBON_MAX_COMMANDS CONFIG_CARBON_MAX_COMMANDS
#else
#define CARBON_MAX_COMMANDS 64
#endif

static const char *TAG = "carbon_registry";
static const carbon_cmd_descriptor_t *s_registry[CARBON_MAX_COMMANDS];
static int s_count = 0;

esp_err_t carbon_register_command(const carbon_cmd_descriptor_t *desc)
{
    if (s_count >= CARBON_MAX_COMMANDS) {
        ESP_LOGE(TAG, "Registry full (max %d commands)", CARBON_MAX_COMMANDS);
        return ESP_ERR_NO_MEM;
    }
    s_registry[s_count++] = desc;
    return ESP_OK;
}

int carbon_registry_count(void)
{
    return s_count;
}

const carbon_cmd_descriptor_t *carbon_registry_get(int index)
{
    if (index < 0 || index >= s_count) return NULL;
    return s_registry[index];
}
