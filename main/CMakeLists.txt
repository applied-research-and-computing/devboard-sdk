idf_component_register(SRCS "main.c"
                            "hislip.c"
                            "hislip_server.c"
                            "scpi_parser.c"
                            "scpi_standard.c"
                            "scpi_gpio.c"
                            "scpi_adc.c"
                            "scpi_uart.c"
                            "mdns_service.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_adc
                                     esp_driver_gpio
                                     esp_driver_uart
                                     esp_event
                                     esp_netif
                                     esp_wifi
                                     lwip
                                     mdns
                                     nvs_flash)
