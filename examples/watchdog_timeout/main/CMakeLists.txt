idf_component_register(
    SRCS "watchdog_timeout_example.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_event carbon_instrument
)
