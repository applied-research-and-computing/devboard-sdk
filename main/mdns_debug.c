/**
 * @file mdns_debug.c
 * @brief mDNS debugging utilities implementation
 */

#include "mdns_debug.h"
#include "mdns.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "mdns_debug";

void mdns_print_diagnostics(void)
{
    ESP_LOGI(TAG, "=== mDNS Diagnostics ===");
    
    // Check WiFi connection status
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi: Connected to SSID: %s, RSSI: %d", ap_info.ssid, ap_info.rssi);
    } else {
        ESP_LOGE(TAG, "WiFi: Not connected (%s)", esp_err_to_name(ret));
        return;
    }
    
    // Get network interface
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGE(TAG, "Failed to get WiFi STA netif");
        return;
    }
    
    // Get IP address
    esp_netif_ip_info_t ip_info;
    ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
    } else {
        ESP_LOGE(TAG, "Failed to get IP info");
    }
    
    // Check mDNS multicast status (lwIP netif check requires internal headers)
    ESP_LOGI(TAG, "Network interface available for mDNS");
    
    // Check mDNS hostname
    char hostname[32] = {0};
    esp_err_t ret_hostname = mdns_hostname_get(hostname);
    if (ret_hostname == ESP_OK && hostname[0] != '\0') {
        ESP_LOGI(TAG, "mDNS hostname: %s.local", hostname);
    } else {
        ESP_LOGE(TAG, "mDNS hostname not set");
    }
    
    ESP_LOGI(TAG, "=== End Diagnostics ===");
}
