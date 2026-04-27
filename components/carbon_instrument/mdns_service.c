/**
 * @file mdns_service.c
 * @brief mDNS service advertisement for HiSLIP instrument discovery
 */

#include "mdns_service.h"
#include "sdkconfig.h"
#include "mdns.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <stdio.h>

static const char *TAG = "mdns_service";

#ifndef CONFIG_DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "carbon-esp32-inst"
#else
#define DEVICE_HOSTNAME CONFIG_DEVICE_HOSTNAME
#endif

#ifndef CONFIG_DEVICE_SERIAL
#define DEVICE_SERIAL "SN12345"
#else
#define DEVICE_SERIAL CONFIG_DEVICE_SERIAL
#endif

#ifndef CONFIG_HISLIP_SYNC_PORT
#define HISLIP_SYNC_PORT 4880
#else
#define HISLIP_SYNC_PORT CONFIG_HISLIP_SYNC_PORT
#endif

esp_err_t mdns_service_init(void)
{
    esp_err_t ret;

    // Initialize mDNS
    ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Generate unique hostname using MAC address suffix
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char hostname[64];
    snprintf(hostname, sizeof(hostname), "%s-%02x%02x", DEVICE_HOSTNAME, mac[4], mac[5]);

    // Set hostname
    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "mDNS hostname set to: %s", hostname);

    // Set instance name
    ret = mdns_instance_name_set("Carbon ESP32 Instrument");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(ret));
        return ret;
    }

    // Advertise HiSLIP service on port (sync channel)
    ret = mdns_service_add(NULL, "_hislip", "_tcp", HISLIP_SYNC_PORT, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add HiSLIP service: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "HiSLIP service advertised on port %d", HISLIP_SYNC_PORT);

    // Add TXT records for service metadata
    mdns_service_txt_item_set("_hislip", "_tcp", "model", "ESP32-INSTRUMENT");
    mdns_service_txt_item_set("_hislip", "_tcp", "vendor", "Carbon");
    mdns_service_txt_item_set("_hislip", "_tcp", "version", "1.0");
    mdns_service_txt_item_set("_hislip", "_tcp", "serial", DEVICE_SERIAL);

    ESP_LOGI(TAG, "mDNS service initialization complete");
    return ESP_OK;
}
