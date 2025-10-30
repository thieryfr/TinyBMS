# HAL IDF Component
# ESP-IDF native implementations of HAL interfaces

idf_component_register(
    SRCS
        "esp32_uart_idf.cpp"
        "esp32_can_idf.cpp"
        "esp32_storage_idf.cpp"
        "esp32_gpio_idf.cpp"
        "esp32_timer_idf.cpp"
        "esp32_watchdog_idf.cpp"
        "esp32_factory_idf.cpp"
    INCLUDE_DIRS
        "."
        "../../include"
    REQUIRES
        driver
        esp_timer
        esp_wifi
        esp_netif
        esp_http_server
        spiffs
        nvs_flash
        vfs
)
