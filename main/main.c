#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "carbon_instrument.h"
#include "carbon_response.h"
#include "carbon_wifi.h"

static const char *TAG = "carbon-hislip";

static int test_slow_handler(const char *cmd, char *r, size_t n)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    return carbon_respond_int(r, n, 1);
}

static const carbon_cmd_descriptor_t test_slow_cmd = {
    .scpi_command = "TEST:SLOW",
    .type         = CARBON_CMD_QUERY,
    .group        = "Test",
    .description  = "Sleeps 2 s then returns 1; used to test concurrent command execution",
    .param_count  = 0,
    .timeout_ms   = 5000,
    .handler      = test_slow_handler,
};

void app_main(void)
{
    ESP_LOGI(TAG, "Carbon HiSLIP Instrument starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!carbon_wifi_init()) {
        ESP_LOGE(TAG, "Network unavailable; HiSLIP server not started");
        return;
    }

    carbon_register_command(&test_slow_cmd);
    carbon_instrument_start();
}
