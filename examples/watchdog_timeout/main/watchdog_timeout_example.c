/**
 * Watchdog Timeout Example for Carbon ESP32 SDK.
 *
 * Demonstrates the built-in command watchdog: every registered handler runs
 * inside a FreeRTOS task monitored by a one-shot software timer. If the
 * handler does not return within its descriptor's timeout_ms, the SDK:
 *
 *   1. Kills the handler task
 *   2. Returns a SCPI error response (-365,"Time out error") to the client
 *   3. Logs a warning via ESP_LOGW
 *   4. Immediately accepts the next command — the device does not hang
 *
 * Two commands are registered:
 *
 *   PING?       — returns instantly; demonstrates unaffected normal operation
 *   SLOW:HANG   — blocks for 10 s with a 2 s timeout; always triggers watchdog
 *
 * Run tools/test_watchdog.py against this firmware to verify both paths.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "carbon_instrument.h"

static const char *TAG = "watchdog_example";

// ============================================================================
// Handlers
// ============================================================================

static int cmd_ping(const char *cmd, char *r, size_t n)
{
    return snprintf(r, n, "PONG");
}

// Simulates a handler stuck waiting on a peripheral that never responds.
// vTaskDelay keeps it blocked well past the 2 s watchdog — the SDK kills it.
static int cmd_slow_hang(const char *cmd, char *r, size_t n)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    return snprintf(r, n, "OK");
}

// ============================================================================
// Command descriptors
// ============================================================================

static const carbon_cmd_descriptor_t ping_cmd = {
    .scpi_command = "PING?",
    .type         = CARBON_CMD_QUERY,
    .group        = "Watchdog Demo",
    .description  = "Immediate response — watchdog unaffected",
    .param_count  = 0,
    .timeout_ms   = 1000,
    .handler      = cmd_ping,
};

static const carbon_cmd_descriptor_t slow_hang_cmd = {
    .scpi_command = "SLOW:HANG",
    .type         = CARBON_CMD_QUERY,
    .group        = "Watchdog Demo",
    .description  = "Always exceeds timeout — triggers watchdog",
    .param_count  = 0,
    .timeout_ms   = 2000,   // watchdog fires after 2 s; handler sleeps 10 s
    .handler      = cmd_slow_hang,
};

// ============================================================================
// WiFi
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
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  Carbon ESP32 SDK - Watchdog Timeout Example");
    ESP_LOGI(TAG, "==============================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(5000));

    carbon_instrument_init(NULL);

    ESP_LOGI(TAG, "Registering commands...");
    carbon_register_command(&ping_cmd);
    carbon_register_command(&slow_hang_cmd);

    ESP_LOGI(TAG, "Starting Carbon SDK...");
    carbon_instrument_start();

    ESP_LOGI(TAG, "Ready. Send SLOW:HANG to trigger the watchdog.");
    ESP_LOGI(TAG, "Run: uv run examples/watchdog_timeout/test_watchdog.py --host <hostname>");
}
