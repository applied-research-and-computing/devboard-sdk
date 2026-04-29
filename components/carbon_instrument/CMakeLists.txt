set(SRCS
    "carbon_instrument.c"
    "carbon_error_queue.c"
    "carbon_registry.c"
    "carbon_param_parser.c"
    "carbon_response.c"
    "hislip.c"
    "hislip_server.c"
    "scpi_parser.c"
    "scpi_watchdog.c"
    "scpi_standard.c"
    "scpi_system.c"
    "mdns_service.c"
    "mdns_debug.c"
)

if(CONFIG_CARBON_ENABLE_GPIO)
    list(APPEND SRCS "scpi_gpio.c")
endif()

if(CONFIG_CARBON_ENABLE_ADC)
    list(APPEND SRCS "scpi_adc.c")
endif()

if(CONFIG_CARBON_ENABLE_UART)
    list(APPEND SRCS "scpi_uart.c")
endif()

# Always declare all peripheral deps — ESP-IDF's include scanner checks every
# .c file in the component directory, not just those in SRCS.
idf_component_register(
    SRCS              ${SRCS}
    INCLUDE_DIRS      "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_adc
        esp_driver_gpio
        esp_driver_uart
        esp_netif
        esp_wifi
        lwip
        mdns
)
