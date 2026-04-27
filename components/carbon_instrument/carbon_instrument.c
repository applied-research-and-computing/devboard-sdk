#include "carbon_instrument.h"
#include "hislip_server.h"
#include "mdns_service.h"
#include "mdns_debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "carbon_instrument";

extern void scpi_standard_init(void);
extern void scpi_gpio_init(void);
extern void scpi_adc_init(void);
extern void scpi_uart_init(void);

void carbon_instrument_start(void)
{
    ESP_LOGI(TAG, "Registering built-in commands");
    scpi_standard_init();
    scpi_gpio_init();
    scpi_adc_init();
    scpi_uart_init();

    ESP_LOGI(TAG, "Starting Carbon instrument services");
    ESP_ERROR_CHECK(mdns_service_init());
    vTaskDelay(pdMS_TO_TICKS(1000));
    mdns_print_diagnostics();
    hislip_server_start();
    ESP_LOGI(TAG, "Carbon instrument ready");
}
