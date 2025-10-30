# Main component for TinyBMS ESP-IDF

idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
        "../include"
    REQUIRES
        hal_idf
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_server
)
