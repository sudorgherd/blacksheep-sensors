idf_component_register(
    SRCS "main.c"
    REQUIRES esp_event esp_netif esp_wifi nvs_flash
)
