idf_component_register(
    SRCS "hello_world_example.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_event carbon_instrument
)
