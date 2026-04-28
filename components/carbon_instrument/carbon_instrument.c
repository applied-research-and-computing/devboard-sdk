#include "carbon_instrument.h"
#include "hislip_server.h"
#include "mdns_service.h"
#include "mdns_debug.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "carbon_instrument";

#ifndef CONFIG_DEVICE_SERIAL
#define DEFAULT_SERIAL "SN12345"
#else
#define DEFAULT_SERIAL CONFIG_DEVICE_SERIAL
#endif

static void (*s_trigger_cb)(void) = NULL;

void carbon_register_trigger(void (*callback)(void))
{
    s_trigger_cb = callback;
}

void carbon_fire_trigger(void)
{
    if (s_trigger_cb != NULL) {
        s_trigger_cb();
    }
}

static carbon_instrument_config_t s_config = {
    .manufacturer     = "CARBON",
    .model            = "ESP32-INSTRUMENT",
    .serial           = DEFAULT_SERIAL,
    .firmware_version = "v1.0.0",
};

void carbon_instrument_init(const carbon_instrument_config_t *config)
{
    if (!config) return;
    if (config->manufacturer)     s_config.manufacturer     = config->manufacturer;
    if (config->model)            s_config.model            = config->model;
    if (config->serial)           s_config.serial           = config->serial;
    if (config->firmware_version) s_config.firmware_version = config->firmware_version;
}

const carbon_instrument_config_t *carbon_instrument_get_config(void)
{
    return &s_config;
}

extern void scpi_standard_init(void);
extern void scpi_system_init(void);
#if CONFIG_CARBON_ENABLE_GPIO
extern void scpi_gpio_init(void);
#endif
#if CONFIG_CARBON_ENABLE_ADC
extern void scpi_adc_init(void);
#endif
#if CONFIG_CARBON_ENABLE_UART
extern void scpi_uart_init(void);
#endif

void carbon_instrument_start(void)
{
    ESP_LOGI(TAG, "Registering built-in commands");
    scpi_standard_init();
    scpi_system_init();
#if CONFIG_CARBON_ENABLE_GPIO
    scpi_gpio_init();
#endif
#if CONFIG_CARBON_ENABLE_ADC
    scpi_adc_init();
#endif
#if CONFIG_CARBON_ENABLE_UART
    scpi_uart_init();
#endif

    ESP_LOGI(TAG, "Starting Carbon instrument services");
    ESP_ERROR_CHECK(mdns_service_init());
    vTaskDelay(pdMS_TO_TICKS(1000));
    mdns_print_diagnostics();
    hislip_server_start();
    ESP_LOGI(TAG, "Carbon instrument ready");
}
