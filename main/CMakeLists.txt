set(extra_requires "")
if(CONFIG_CARBON_ENABLE_WIFI_PROVISIONING)
    list(APPEND extra_requires network_provisioning)
    if(CONFIG_CARBON_PROVISION_BLE)
        list(APPEND extra_requires bt)
    endif()
    if(CONFIG_CARBON_PROVISION_GPIO_ENABLE)
        list(APPEND extra_requires driver)
    endif()
endif()

idf_component_register(SRCS "main.c" "carbon_wifi.c"
                       PRIV_REQUIRES carbon_instrument
                                     esp_event
                                     esp_netif
                                     esp_wifi
                                     esp_system
                                     nvs_flash
                                     ${extra_requires})
