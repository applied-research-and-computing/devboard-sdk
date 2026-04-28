/**
 * Minimal "Hello World" example for Carbon ESP32 SDK.
 *
 * Shows the simplest possible use case:
 *   1. Write a custom handler function
 *   2. Register it with a carbon_cmd_descriptor_t
 *   3. Call carbon_instrument_start() — done
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "carbon_instrument.h"
#include "carbon_response.h"

static const char *TAG = "hello_world";

// ============================================================================
// YOUR CUSTOM HANDLERS
// Signature: int handler(const char *cmd, char *response, size_t response_max)
// cmd      — normalized SCPI string, e.g. "GREET:NAME \"Alice\""
// response — write your response here; return its length
// ============================================================================

static int cmd_hello(const char *cmd, char *r, size_t n)
{
    return snprintf(r, n, "Hello, World!");
}

// cmd arrives as: GREET:NAME "Alice"
// Extract the quoted argument that follows the mnemonic.
static int cmd_greet_name(const char *cmd, char *r, size_t n)
{
    const char *arg = strchr(cmd, ' ');
    if (!arg) return snprintf(r, n, "ERROR: Missing name parameter");
    arg++;
    if (*arg == '"') arg++;
    const char *end = strchr(arg, '"');
    int len = end ? (int)(end - arg) : (int)strlen(arg);
    return snprintf(r, n, "Hello, %.*s! Welcome to Carbon SDK.", len, arg);
}

static int cmd_counter(const char *cmd, char *r, size_t n)
{
    static int64_t count = 0;
    count++;
    return carbon_respond_int(r, n, count);
}

// cmd arrives as: ECHO "your message"
static int cmd_echo(const char *cmd, char *r, size_t n)
{
    const char *arg = strchr(cmd, ' ');
    if (!arg) return snprintf(r, n, "ERROR: Missing message parameter");
    arg++;
    if (*arg == '"') arg++;
    const char *end = strchr(arg, '"');
    int len = end ? (int)(end - arg) : (int)strlen(arg);
    return snprintf(r, n, "%.*s", len, arg);
}

// ============================================================================
// Command descriptors — must be static (they must outlive registration)
// ============================================================================

static const carbon_cmd_descriptor_t hello_cmd = {
    .scpi_command = "HELLO?",
    .type         = CARBON_CMD_QUERY,
    .group        = "Hello World",
    .description  = "Returns a friendly greeting",
    .param_count  = 0,
    .timeout_ms   = 1000,
    .handler      = cmd_hello,
};

static const carbon_cmd_descriptor_t greet_name_cmd = {
    .scpi_command = "GREET:NAME",
    .type         = CARBON_CMD_WRITE,
    .group        = "Hello World",
    .description  = "Personalized greeting with a name parameter",
    .param_count  = 1,
    .params = {{
        .name        = "name",
        .type        = CARBON_PARAM_STRING,
        .description = "Name to greet",
    }},
    .timeout_ms   = 1000,
    .handler      = cmd_greet_name,
};

static const carbon_cmd_descriptor_t counter_cmd = {
    .scpi_command = "COUNTER?",
    .type         = CARBON_CMD_QUERY,
    .group        = "Hello World",
    .description  = "Increments and returns a counter value",
    .param_count  = 0,
    .timeout_ms   = 1000,
    .handler      = cmd_counter,
};

static const carbon_cmd_descriptor_t echo_cmd = {
    .scpi_command = "ECHO",
    .type         = CARBON_CMD_WRITE,
    .group        = "Hello World",
    .description  = "Echoes back the provided message",
    .param_count  = 1,
    .params = {{
        .name        = "message",
        .type        = CARBON_PARAM_STRING,
        .description = "Message to echo",
    }},
    .timeout_ms   = 1000,
    .handler      = cmd_echo,
};

// ============================================================================
// WiFi setup
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi initialized, connecting to %s...", CONFIG_WIFI_SSID);
}

// ============================================================================
// Main
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  Carbon ESP32 SDK - Hello World Example");
    ESP_LOGI(TAG, "===========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Step 1: (optional) override device identity
    carbon_instrument_init(NULL);

    // Step 2: Register your custom commands
    ESP_LOGI(TAG, "Registering custom commands...");
    carbon_register_command(&hello_cmd);
    carbon_register_command(&greet_name_cmd);
    carbon_register_command(&counter_cmd);
    carbon_register_command(&echo_cmd);

    // Step 3: Start (HiSLIP server, mDNS, built-in SCPI commands)
    ESP_LOGI(TAG, "Starting Carbon SDK...");
    carbon_instrument_start();

    ESP_LOGI(TAG, "Hello World example running!");
    ESP_LOGI(TAG, "Try: carbond exec carbon_esp32_instrument.yaml hello");
}
