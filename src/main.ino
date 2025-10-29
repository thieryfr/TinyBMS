/**
 * @file main.ino
 * @brief TinyBMS to Victron CAN-BMS Bridge with FreeRTOS + Logging System
 */

#include <Arduino.h>
#include "rtos_tasks.h"
#include "system_init.h"
#include "shared_data.h"
#include "watchdog_manager.h"
#include "tinybms_victron_bridge.h"
#include "rtos_config.h"
#include "config_manager.h"
#include "logger.h"
#include "event/event_bus_v2.h"
#include "event/event_types_v2.h"
#include "mqtt/victron_mqtt_bridge.h"
#include "tinybms_config_editor.h"
#include "hal/hal_manager.h"
#include "hal/hal_config.h"
#include "hal/esp32_factory.h"

#include <exception>

// Global resources
SemaphoreHandle_t uartMutex;
SemaphoreHandle_t feedMutex;
SemaphoreHandle_t configMutex;
SemaphoreHandle_t liveMutex;   // Protects bridge.live_data_ access
SemaphoreHandle_t statsMutex;  // Protects bridge.stats access

// Web Server objects
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

ConfigManager config;
WatchdogManager Watchdog;
TinyBMS_Victron_Bridge bridge;
Logger logger;
TinyBMSConfigEditor configEditor;

using tinybms::event::eventBus;

mqtt::VictronMqttBridge mqttBridge(eventBus);

// Task handles
TaskHandle_t webServerTaskHandle = NULL;
TaskHandle_t websocketTaskHandle = NULL;
TaskHandle_t watchdogTaskHandle = NULL;
TaskHandle_t mqttTaskHandle = NULL;

void setup() {
    Serial.begin(115200);

    // Create mutexes early for configuration and logging dependencies
    uartMutex = xSemaphoreCreateMutex();
    feedMutex = xSemaphoreCreateMutex();
    configMutex = xSemaphoreCreateMutex();
    liveMutex = xSemaphoreCreateMutex();   // Phase 1: Fix race condition on live_data_
    statsMutex = xSemaphoreCreateMutex();  // Phase 1: Fix race condition on stats

    if (!uartMutex || !feedMutex || !configMutex || !liveMutex || !statsMutex) {
        Serial.println("[INIT] ❌ Mutex creation failed");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("[INIT] All mutexes created (uart, feed, config, live, stats)");

    // Initialize HAL factory and hardware interfaces
    hal::setFactory(hal::createEsp32Factory());

    auto applyHalConfig = [&]() {
        hal::HalConfig hal_cfg{};
        hal_cfg.uart.rx_pin = config.hardware.uart.rx_pin;
        hal_cfg.uart.tx_pin = config.hardware.uart.tx_pin;
        hal_cfg.uart.baudrate = config.hardware.uart.baudrate;
        hal_cfg.uart.timeout_ms = config.hardware.uart.timeout_ms;
        hal_cfg.uart.use_dma = true;

        hal_cfg.can.tx_pin = config.hardware.can.tx_pin;
        hal_cfg.can.rx_pin = config.hardware.can.rx_pin;
        hal_cfg.can.bitrate = config.hardware.can.bitrate;
        hal_cfg.can.enable_termination = config.hardware.can.termination;

        hal_cfg.storage.type = config.advanced.enable_spiffs ? hal::StorageType::SPIFFS : hal::StorageType::NVS;
        hal_cfg.storage.format_on_fail = true;
        hal_cfg.watchdog.timeout_ms = config.advanced.watchdog_timeout_s * 1000;

        try {
            hal::HalManager::instance().initialize(hal_cfg);
        } catch (const std::exception& ex) {
            Serial.println(String("[HAL] ❌ Initialization failed: ") + ex.what());
        }
    };

    applyHalConfig();

    // Load configuration (uses configMutex internally)
    if (!config.begin()) {
        Serial.println("[CONFIG] ⚠ Using default configuration");
    } else {
        Serial.println("[CONFIG] Configuration loaded");
        applyHalConfig();
    }

    const uint32_t desired_baud = config.logging.serial_baudrate > 0 ?
        config.logging.serial_baudrate : 115200;
    if (desired_baud != 115200) {
        Serial.begin(desired_baud);
    }

    // Initialize Logger (after configuration)
    if (!logger.begin(config)) {
        Serial.println("[LOGGER] ⚠ Failed to initialize logging to SPIFFS, Serial logging only");
    } else {
        logger.log(LOG_INFO, "Logging system initialized");
    }

    // Initialize watchdog using configured timeout
    if (!Watchdog.begin(config.advanced.watchdog_timeout_s * 1000)) {
        logger.log(LOG_ERROR, "Watchdog initialization failed");
    } else {
        logger.log(LOG_INFO, "Watchdog started");
    }

    // Initialize remaining subsystems
    if (!initializeSystem()) {
        logger.log(LOG_ERROR, "System initialization completed with errors");
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY); // FreeRTOS runs everything
}
