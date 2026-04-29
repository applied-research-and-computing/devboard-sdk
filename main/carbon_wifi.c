#include "carbon_wifi.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"

#ifdef CONFIG_CARBON_ENABLE_WIFI_PROVISIONING
#include "network_provisioning/manager.h"
#include "network_provisioning/network_config.h"
#ifdef CONFIG_CARBON_PROVISION_BLE
#include "network_provisioning/scheme_ble.h"
#else
#include "network_provisioning/scheme_softap.h"
#endif
#ifdef CONFIG_CARBON_PROVISION_GPIO_ENABLE
#include "driver/gpio.h"
#endif
#endif

static const char *TAG = "carbon-wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* Suppresses STA connect/retry logic while the provisioning manager is active.
 * Written from event handler, read from main task — volatile prevents caching. */
static volatile bool s_prov_active = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }

#ifdef CONFIG_CARBON_ENABLE_WIFI_PROVISIONING
    if (base == NETWORK_PROV_EVENT) {
        switch (id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "WiFi provisioning started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            network_prov_config_set_wifi_data_t *cfg = (network_prov_config_set_wifi_data_t *)data;
            ESP_LOGI(TAG, "Credentials received for SSID: %.32s", cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL: {
            network_prov_wifi_sta_fail_reason_t *reason = (network_prov_wifi_sta_fail_reason_t *)data;
            ESP_LOGE(TAG, "Provisioning credential failure: %s",
                     *reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR ? "auth error" : "AP not found");
            network_prov_mgr_reset_wifi_sm_state_on_failure();
            break;
        }
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning credentials accepted");
            break;
        case NETWORK_PROV_END:
            s_prov_active = false;
            network_prov_mgr_deinit();
            break;
        }
        return;
    }
#endif

    if (s_prov_active) {
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, connecting...");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_CARBON_WIFI_MAX_RETRY) {
            s_retry_num++;
            ESP_LOGI(TAG, "WiFi retry %d/%d", s_retry_num, CONFIG_CARBON_WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", CONFIG_CARBON_WIFI_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
}

#ifdef CONFIG_CARBON_ENABLE_WIFI_PROVISIONING
static bool provision_gpio_held(void)
{
#ifdef CONFIG_CARBON_PROVISION_GPIO_ENABLE
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_CARBON_PROVISION_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    /* Active low: button connects GPIO to GND */
    bool held = (gpio_get_level(CONFIG_CARBON_PROVISION_GPIO) == 0);
    gpio_reset_pin(CONFIG_CARBON_PROVISION_GPIO);  /* Boot-time check only; release for other uses */
    if (held) {
        ESP_LOGW(TAG, "GPIO %d held low: forcing re-provisioning", CONFIG_CARBON_PROVISION_GPIO);
    }
    return held;
#else
    return false;
#endif
}
#endif

bool carbon_wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

#ifdef CONFIG_CARBON_ENABLE_WIFI_PROVISIONING
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));

    network_prov_mgr_config_t prov_cfg = {
#ifdef CONFIG_CARBON_PROVISION_BLE
        .scheme               = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
#else
        .scheme               = network_prov_scheme_softap,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
#endif
    };
    ESP_ERROR_CHECK(network_prov_mgr_init(prov_cfg));

    bool provisioned = false;
    ESP_ERROR_CHECK(network_prov_mgr_is_wifi_provisioned(&provisioned));

    bool force_reprov = !provisioned || provision_gpio_held();
    if (force_reprov) {
        if (provisioned) {
            /* GPIO held: clear stored credentials so we re-provision fresh */
            network_prov_mgr_reset_wifi_provisioning();
        }
        s_prov_active = true;

        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        char service_name[20];
        snprintf(service_name, sizeof(service_name),
                 "CARBON-%02X%02X%02X", mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "Entering provisioning mode, service name: \"%s\"", service_name);

        esp_netif_create_default_wifi_sta();
#ifndef CONFIG_CARBON_PROVISION_BLE
        esp_netif_create_default_wifi_ap();
#endif

        network_prov_security_t security =
#ifdef CONFIG_CARBON_PROVISION_SECURITY_0
            NETWORK_PROV_SECURITY_0;
#else
            NETWORK_PROV_SECURITY_1;
#endif
        const char *pop = (strlen(CONFIG_CARBON_PROVISION_POP) > 0)
                          ? CONFIG_CARBON_PROVISION_POP : NULL;

        ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(
            security, pop, service_name, NULL));

        /* Blocks until NETWORK_PROV_END; IP is obtained before this returns */
        esp_err_t prov_err = network_prov_mgr_wait();
        if (prov_err != ESP_OK) {
            ESP_LOGE(TAG, "Provisioning error: %s", esp_err_to_name(prov_err));
            network_prov_mgr_deinit();
            return false;
        }
        /* network_prov_mgr_deinit() already called from NETWORK_PROV_END handler */
    } else {
        /* Already provisioned: connect using NVS-stored credentials */
        network_prov_mgr_deinit();
        esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
#else
    esp_netif_create_default_wifi_sta();
    wifi_config_t wifi_config = {
        .sta = {
            .ssid               = CONFIG_WIFI_SSID,
            .password           = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
#endif

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return true;
    }

#ifdef CONFIG_CARBON_ENABLE_WIFI_PROVISIONING
    /* Stored credentials exhausted retries; reset NVS and reboot to provisioning mode.
     * Done here (main task) rather than in the event handler to avoid blocking NVS
     * operations inside an event callback. */
    ESP_LOGW(TAG, "Erasing stored credentials; rebooting into provisioning mode");
    network_prov_mgr_reset_wifi_provisioning();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
#endif

    ESP_LOGE(TAG, "Network unavailable");
    return false;
}
