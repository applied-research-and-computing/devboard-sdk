#pragma once
#include <stdbool.h>

/*
 * Initialize WiFi and block until an IP is obtained.
 *
 * When CARBON_ENABLE_WIFI_PROVISIONING is enabled:
 *   - First boot or GPIO held at boot → enters SoftAP/BLE provisioning mode.
 *   - Credentials stored in NVS; subsequent boots connect automatically.
 *   - If stored credentials fail after CARBON_WIFI_MAX_RETRY retries, stored
 *     credentials are erased and the device reboots into provisioning mode.
 *
 * When CARBON_ENABLE_WIFI_PROVISIONING is disabled:
 *   - Connects using compile-time WIFI_SSID / WIFI_PASSWORD from sdkconfig.
 *
 * Returns true on successful connection, false if provisioning is disabled
 * and the connection could not be established.
 */
bool carbon_wifi_init(void);
