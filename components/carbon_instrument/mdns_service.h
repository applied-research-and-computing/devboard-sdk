/**
 * @file mdns_service.h
 * @brief mDNS service advertisement for HiSLIP instrument discovery
 */

#ifndef MDNS_SERVICE_H
#define MDNS_SERVICE_H

#include "esp_err.h"

/**
 * @brief Initialize mDNS responder and advertise HiSLIP service
 * 
 * Sets hostname, instance name, and registers _hislip._tcp service
 * for network discovery. Should be called after WiFi connection is established.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mdns_service_init(void);

#endif // MDNS_SERVICE_H
