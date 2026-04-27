idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES carbon_instrument
                                     esp_event
                                     esp_netif
                                     esp_wifi
                                     nvs_flash)
