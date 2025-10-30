/**
 * @file main.cpp
 * @brief TinyBMS ESP-IDF Main Entry Point (Phase 1)
 *
 * Phase 1: Fondations ESP-IDF
 * Minimal main file to test ESP-IDF build and HAL initialization
 *
 * This file demonstrates:
 * - ESP-IDF app_main() entry point
 * - HAL Factory IDF initialization
 * - Basic HAL component testing
 *
 * NOTE: This is a minimal test harness for Phase 1.
 *       Full TinyBMS functionality will be integrated in Phase 2.
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// HAL IDF includes
#include "hal/hal_factory.h"
#include "components/hal_idf/esp32_factory_idf.h"

static const char* TAG = "TinyBMS-IDF";

/**
 * @brief Initialize NVS (Non-Volatile Storage)
 */
static void initialize_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

/**
 * @brief Test HAL components initialization
 */
static void test_hal_components() {
    ESP_LOGI(TAG, "Testing HAL IDF components...");

    // Create HAL Factory IDF
    auto factory = std::make_unique<hal::ESP32FactoryIDF>();

    // Test UART creation
    auto uart = factory->createUart();
    if (uart) {
        ESP_LOGI(TAG, "✓ UART HAL created successfully");

        // Test configuration (GPIO16/17, 19200 baud - TinyBMS default)
        hal::UartConfig uart_config{};
        uart_config.rx_pin = 16;
        uart_config.tx_pin = 17;
        uart_config.baudrate = 19200;
        uart_config.timeout_ms = 100;

        hal::Status status = uart->initialize(uart_config);
        if (status == hal::Status::Ok) {
            ESP_LOGI(TAG, "✓ UART initialized successfully");
        } else {
            ESP_LOGW(TAG, "✗ UART initialization failed");
        }
    }

    // Test CAN creation
    auto can = factory->createCan();
    if (can) {
        ESP_LOGI(TAG, "✓ CAN HAL created successfully");

        // Test configuration (GPIO4/5, 500kbps - Victron default)
        hal::CanConfig can_config{};
        can_config.tx_pin = 4;
        can_config.rx_pin = 5;
        can_config.bitrate = 500000;
        can_config.enable_termination = true;

        hal::Status status = can->initialize(can_config);
        if (status == hal::Status::Ok) {
            ESP_LOGI(TAG, "✓ CAN initialized successfully");
        } else {
            ESP_LOGW(TAG, "✗ CAN initialization failed");
        }
    }

    // Test Storage creation
    auto storage = factory->createStorage();
    if (storage) {
        ESP_LOGI(TAG, "✓ Storage HAL created successfully");

        hal::StorageConfig storage_config{};
        storage_config.type = hal::StorageType::SPIFFS;
        storage_config.format_on_fail = false;

        hal::Status status = storage->mount(storage_config);
        if (status == hal::Status::Ok) {
            ESP_LOGI(TAG, "✓ Storage mounted successfully");

            // Test file existence
            bool exists = storage->exists("/config.json");
            ESP_LOGI(TAG, "  config.json exists: %s", exists ? "yes" : "no");
        } else {
            ESP_LOGW(TAG, "✗ Storage mount failed (SPIFFS partition may not exist yet)");
        }
    }

    // Test GPIO creation
    auto gpio = factory->createGpio();
    if (gpio) {
        ESP_LOGI(TAG, "✓ GPIO HAL created successfully");
    }

    // Test Timer creation
    auto timer = factory->createTimer();
    if (timer) {
        ESP_LOGI(TAG, "✓ Timer HAL created successfully");
    }

    // Test Watchdog creation
    auto watchdog = factory->createWatchdog();
    if (watchdog) {
        ESP_LOGI(TAG, "✓ Watchdog HAL created successfully");
    }

    ESP_LOGI(TAG, "HAL IDF components test completed!");
}

/**
 * @brief Main task for testing
 */
static void main_task(void* pvParameters) {
    ESP_LOGI(TAG, "TinyBMS ESP-IDF Phase 1 - Main Task Started");

    // Test HAL components
    test_hal_components();

    // Print system info
    ESP_LOGI(TAG, "=== System Info ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    // Keep task alive
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10 seconds

        // Print periodic status
        ESP_LOGI(TAG, "Uptime: %lld ms, Free heap: %lu bytes",
                 esp_timer_get_time() / 1000,
                 esp_get_free_heap_size());
    }
}

/**
 * @brief ESP-IDF application entry point
 *
 * This replaces Arduino's setup()/loop() pattern
 */
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "TinyBMS ESP-IDF Migration - Phase 1");
    ESP_LOGI(TAG, "Testing HAL IDF Components");
    ESP_LOGI(TAG, "============================================");

    // Initialize NVS
    initialize_nvs();

    // Create main task
    xTaskCreate(main_task, "main_task", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "app_main() completed, scheduler running");
}
