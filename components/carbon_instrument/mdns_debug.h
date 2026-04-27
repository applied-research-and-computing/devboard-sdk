/**
 * @file mdns_debug.h
 * @brief mDNS debugging utilities
 */

#ifndef MDNS_DEBUG_H
#define MDNS_DEBUG_H

#include "esp_err.h"

/**
 * @brief Print mDNS diagnostic information
 * 
 * Checks network interface status, multicast group membership,
 * and mDNS service registration.
 */
void mdns_print_diagnostics(void);

#endif // MDNS_DEBUG_H
