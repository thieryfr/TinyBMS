/**
 * @file system_init.cpp
 * @brief System initialization module with FreeRTOS + Logging
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <cstring>
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "shared_data.h"
#include "logger.h"
#include "config_manager.h"
#include "tinybms_victron_bridge.h"
#include "tinybms_config_editor.h"
#include "event/event_bus_v2.h"
#include "event/event_types_v2.h"
#include "bridge_core.h"
#include "tiny_read_mapping.h"
#include "victron_can_mapping.h"
#include "mqtt/victron_mqtt_bridge.h"
#include "hal/hal_manager.h"
#include "hal/hal_config.h"
#include "hal/hal_factory.h"
#include "hal/esp32_factory.h"

// Watchdog integration
#include "watchdog_manager.h"
extern WatchdogManager Watchdog;

// External globals (from main.ino)
extern ConfigManager config;
extern TinyBMS_Victron_Bridge bridge;
extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t feedMutex;
extern Logger logger;
extern TinyBMSConfigEditor configEditor;
using tinybms::event::eventBus;
using tinybms::events::EventSource;
using tinybms::events::StatusLevel;
using tinybms::events::StatusMessage;
using tinybms::events::LiveDataUpdate;
extern mqtt::VictronMqttBridge mqttBridge;

extern TaskHandle_t webServerTaskHandle;
extern TaskHandle_t websocketTaskHandle;
extern TaskHandle_t watchdogTaskHandle;
extern TaskHandle_t mqttTaskHandle;

hal::HalConfig buildHalConfig(const ConfigManager& cfg) {
    hal::HalConfig hal_cfg{};
    hal_cfg.uart.rx_pin = cfg.hardware.uart.rx_pin;
    hal_cfg.uart.tx_pin = cfg.hardware.uart.tx_pin;
    hal_cfg.uart.baudrate = cfg.hardware.uart.baudrate;
    hal_cfg.uart.timeout_ms = cfg.hardware.uart.timeout_ms;
    hal_cfg.uart.use_dma = true;

    hal_cfg.can.tx_pin = cfg.hardware.can.tx_pin;
    hal_cfg.can.rx_pin = cfg.hardware.can.rx_pin;
    hal_cfg.can.bitrate = cfg.hardware.can.bitrate;
    hal_cfg.can.enable_termination = cfg.hardware.can.termination;

    hal_cfg.storage.type = cfg.advanced.enable_spiffs ? hal::StorageType::SPIFFS : hal::StorageType::NVS;
    hal_cfg.storage.format_on_fail = true;

    hal_cfg.watchdog.timeout_ms = cfg.advanced.watchdog_timeout_s * 1000;

    return hal_cfg;
}

namespace {

void feedWatchdogSafely() {
    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }
}

void publishStatusIfPossible(const char* message, StatusLevel level) {
    if (message == nullptr) {
        return;
    }

    StatusMessage event{};
    event.metadata.source = EventSource::System;
    event.level = level;
    std::strncpy(event.message, message, sizeof(event.message) - 1);
    event.message[sizeof(event.message) - 1] = '\0';
    eventBus.publish(event);
}

bool createTask(const char* name,
                TaskFunction_t task,
                uint32_t stack,
                void* params,
                UBaseType_t priority,
                TaskHandle_t* handle) {
    BaseType_t result = xTaskCreate(task, name, stack, params, priority, handle);
    if (result != pdPASS) {
        logger.log(LOG_ERROR, String("[TASK] Failed to create ") + name);
        return false;
    }

    logger.log(LOG_INFO, String("[TASK] ") + name + " created ✓");
    return true;
}

void mqttLoopTask(void* pvParameters) {
    auto* client = static_cast<mqtt::VictronMqttBridge*>(pvParameters);
    const TickType_t delay = pdMS_TO_TICKS(1000);
    while (true) {
        if (client) {
            client->loop();
        }
        vTaskDelay(delay);
    }
}

} // namespace

// ===================================================================================
// WiFi Initialization
// ===================================================================================
bool initializeWiFi() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   WiFi Configuration");
    logger.log(LOG_INFO, "========================================");

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "[WiFi] Failed to acquire config mutex");
        publishStatusIfPossible("WiFi configuration mutex unavailable", StatusLevel::Error);
        return false;
    }

    WiFi.mode(config.wifi.mode.equalsIgnoreCase("ap") ? WIFI_AP : WIFI_STA);
    WiFi.setHostname(config.wifi.sta_hostname.c_str());
    logger.log(LOG_INFO, "[WiFi] Connecting to SSID: " + config.wifi.sta_ssid);

    if (config.wifi.sta_ip_mode.equalsIgnoreCase("static") &&
        config.wifi.sta_static_ip.length() > 0) {
        IPAddress ip, gateway, subnet;
        if (ip.fromString(config.wifi.sta_static_ip) &&
            gateway.fromString(config.wifi.sta_gateway) &&
            subnet.fromString(config.wifi.sta_subnet)) {
            WiFi.config(ip, gateway, subnet);
            logger.log(LOG_INFO, String("[WiFi] Static IP configured: ") + ip.toString());
        } else {
            logger.log(LOG_WARN, "[WiFi] Invalid static IP configuration, falling back to DHCP");
        }
    }

    WiFi.begin(config.wifi.sta_ssid.c_str(), config.wifi.sta_password.c_str());

    uint8_t attempts = 0;
    const uint8_t MAX_ATTEMPTS = 20;

    while (WiFi.status() != WL_CONNECTED && attempts < MAX_ATTEMPTS) {
        feedWatchdogSafely();
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }

    bool success = false;

    if (WiFi.status() == WL_CONNECTED) {
        logger.log(LOG_INFO, "[WiFi] Connected ✓");
        logger.log(LOG_INFO, "[WiFi] IP Address: " + WiFi.localIP().toString());
        logger.log(LOG_INFO, "[WiFi] Hostname: " + config.wifi.sta_hostname);
        logger.log(LOG_INFO, "[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
        publishStatusIfPossible("WiFi client connected", StatusLevel::Notice);
        success = true;
    } else if (config.wifi.ap_fallback.enabled) {
        logger.log(LOG_WARN, "[WiFi] Connection failed - starting AP mode");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(config.wifi.ap_fallback.ssid.c_str(), config.wifi.ap_fallback.password.c_str());
        logger.log(LOG_INFO, "[WiFi] AP Mode started ✓");
        logger.log(LOG_INFO, "[WiFi] AP SSID: " + config.wifi.ap_fallback.ssid);
        logger.log(LOG_INFO, "[WiFi] AP IP: " + WiFi.softAPIP().toString());
        publishStatusIfPossible("WiFi AP fallback active", StatusLevel::Warning);
        success = true;
    } else {
        logger.log(LOG_ERROR, "[WiFi] Connection failed and AP fallback disabled");
        publishStatusIfPossible("WiFi unavailable (connection failed)", StatusLevel::Error);
    }

    xSemaphoreGive(configMutex);
    return success;
}

// ===================================================================================
// SPIFFS Initialization
// ===================================================================================
bool initializeSPIFFS() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   Storage");
    logger.log(LOG_INFO, "========================================");

    feedWatchdogSafely();
    if (!config.advanced.enable_spiffs) {
        logger.log(LOG_INFO, "[Storage] SPIFFS disabled via configuration");
        publishStatusIfPossible("SPIFFS disabled", StatusLevel::Notice);
        return true;
    }

    logger.log(LOG_INFO, "[SPIFFS] Filesystem ready via HAL");
    logger.log(LOG_DEBUG, "[SPIFFS] Total space: " + String(SPIFFS.totalBytes()) + " bytes");
    logger.log(LOG_DEBUG, "[SPIFFS] Used space: " + String(SPIFFS.usedBytes()) + " bytes");

    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    uint32_t total_size = 0;
    uint8_t file_count = 0;

    while (file) {
        logger.log(LOG_DEBUG, String("  - ") + file.name() + " (" + String(file.size()) + " bytes)");
        total_size += file.size();
        file_count++;
        file = root.openNextFile();
    }

    logger.log(LOG_DEBUG, "[SPIFFS] " + String(file_count) + " files, total " + String(total_size) + " bytes");
    publishStatusIfPossible("SPIFFS mounted", StatusLevel::Notice);
    return true;
}

// ===================================================================================
// Bridge Initialization
// ===================================================================================
bool initializeBridge() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   Bridge Initialization");
    logger.log(LOG_INFO, "========================================");

    feedWatchdogSafely();

    BridgeEventSink& sink = defaultBridgeEventSink(eventBus);
    bool success = Bridge_BuildAndBegin(bridge, sink);

    if (!success) {
        logger.log(LOG_ERROR, "[BRIDGE] Initialization failed!");
        logger.log(LOG_WARN, "[BRIDGE] Continuing without bridge (web interface still available)");
        publishStatusIfPossible("Bridge unavailable", StatusLevel::Error);

        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            logger.log(LOG_DEBUG, "  UART RX: GPIO" + String(config.hardware.uart.rx_pin));
            logger.log(LOG_DEBUG, "  UART TX: GPIO" + String(config.hardware.uart.tx_pin));
            logger.log(LOG_DEBUG, "  CAN TX: GPIO" + String(config.hardware.can.tx_pin));
            logger.log(LOG_DEBUG, "  CAN RX: GPIO" + String(config.hardware.can.rx_pin));
            xSemaphoreGive(configMutex);
        }
    } else {
        logger.log(LOG_INFO, "[BRIDGE] Initialized successfully ✓");
        publishStatusIfPossible("Bridge ready", StatusLevel::Notice);

        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            logger.log(LOG_DEBUG, "[CONFIG] Bridge configuration:");
            logger.log(LOG_DEBUG, "  UART RX: GPIO" + String(config.hardware.uart.rx_pin));
            logger.log(LOG_DEBUG, "  UART TX: GPIO" + String(config.hardware.uart.tx_pin));
            logger.log(LOG_DEBUG, "  UART Baudrate: " + String(config.hardware.uart.baudrate));
            logger.log(LOG_DEBUG, "  CAN TX: GPIO" + String(config.hardware.can.tx_pin));
            logger.log(LOG_DEBUG, "  CAN RX: GPIO" + String(config.hardware.can.rx_pin));
            logger.log(LOG_DEBUG, "  CAN Bitrate: " + String(config.hardware.can.bitrate));
            logger.log(LOG_DEBUG, "  CVL Algorithm: " + String(config.cvl.enabled ? "Enabled" : "Disabled"));
            xSemaphoreGive(configMutex);
        }
    }

    return success;
}

bool initializeMqttBridge() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   MQTT Victron Bridge");
    logger.log(LOG_INFO, "========================================");

    ConfigManager::MqttConfig mqtt_cfg{};
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        mqtt_cfg = config.mqtt;
        xSemaphoreGive(configMutex);
    } else {
        logger.log(LOG_WARN, "[MQTT] Using default MQTT configuration (mutex unavailable)");
    }

    mqttBridge.enable(mqtt_cfg.enabled);

    if (!mqtt_cfg.enabled) {
        logger.log(LOG_INFO, "[MQTT] Disabled via configuration");
        publishStatusIfPossible("MQTT bridge disabled", StatusLevel::Notice);
        return true;
    }

    if (!mqttBridge.begin()) {
        publishStatusIfPossible("MQTT event subscription failed", StatusLevel::Error);
        return false;
    }

    mqtt::BrokerSettings broker{};
    broker.uri = mqtt_cfg.uri;
    broker.port = mqtt_cfg.port;
    broker.client_id = mqtt_cfg.client_id;
    broker.username = mqtt_cfg.username;
    broker.password = mqtt_cfg.password;
    broker.root_topic = mqtt_cfg.root_topic;
    broker.clean_session = mqtt_cfg.clean_session;
    broker.use_tls = mqtt_cfg.use_tls;
    broker.server_certificate = mqtt_cfg.server_certificate;
    broker.keepalive_seconds = mqtt_cfg.keepalive_seconds;
    broker.reconnect_interval_ms = mqtt_cfg.reconnect_interval_ms;
    broker.default_qos = mqtt_cfg.default_qos;
    broker.retain_by_default = mqtt_cfg.retain_by_default;

    mqttBridge.configure(broker);

    bool connected = mqttBridge.connect();
    if (connected) {
        publishStatusIfPossible("MQTT bridge connected", StatusLevel::Notice);
    } else {
        publishStatusIfPossible("MQTT bridge connection failed", StatusLevel::Warning);
    }

    bool task_ok = createTask(
        "MQTT",
        mqttLoopTask,
        4096,
        &mqttBridge,
        TASK_NORMAL_PRIORITY,
        &mqttTaskHandle
    );

    if (!task_ok) {
        logger.log(LOG_ERROR, "[MQTT] Failed to create loop task");
        publishStatusIfPossible("MQTT loop task failed", StatusLevel::Error);
    }

    return task_ok;
}

// ===================================================================================
// Config Editor Initialization (placeholder for future implementation)
// ===================================================================================
bool initializeConfigEditor() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   TinyBMS Config Editor");
    logger.log(LOG_INFO, "========================================");

    feedWatchdogSafely();

    configEditor.begin();
    logger.log(LOG_INFO, "[CONFIG_EDITOR] Register catalog ready");
    publishStatusIfPossible("Config editor ready", StatusLevel::Notice);
    return true;
}

// ===================================================================================
// Global System Initialization
// ===================================================================================
bool initializeSystem() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   System Initialization");
    logger.log(LOG_INFO, "========================================");

    bool overall_ok = true;

    const bool spiffs_ok = initializeSPIFFS();
    overall_ok &= spiffs_ok;

    bool mapping_ok = false;
    bool can_mapping_ok = false;
    if (spiffs_ok) {
        mapping_ok = initializeTinyReadMapping(SPIFFS, "/tiny_read.json", &logger);
        if (mapping_ok) {
            publishStatusIfPossible("tiny_read mapping loaded", StatusLevel::Notice);
        } else {
            logger.log(LOG_WARN, "[MAPPING] Failed to load /tiny_read.json");
            publishStatusIfPossible("tiny_read mapping unavailable", StatusLevel::Warning);
        }

        can_mapping_ok = initializeVictronCanMapping(SPIFFS, "/tiny_read_4vic.json", &logger);
        if (can_mapping_ok) {
            publishStatusIfPossible("Victron CAN mapping loaded", StatusLevel::Notice);
        } else {
            logger.log(LOG_WARN, "[CAN_MAP] Failed to load /tiny_read_4vic.json");
            publishStatusIfPossible("Victron CAN mapping unavailable", StatusLevel::Warning);
        }
    } else {
        logger.log(LOG_WARN, "[MAPPING] Skipping tiny_read mapping (SPIFFS unavailable)");
    }

    eventBus.resetStats();
    const bool event_bus_ok = true;
    logger.log(LOG_INFO, "[EVENT_BUS] Ready ✓");
    publishStatusIfPossible("Event bus ready", StatusLevel::Notice);
    publishStatusIfPossible(spiffs_ok ? "SPIFFS mounted" : "SPIFFS unavailable",
                            spiffs_ok ? StatusLevel::Notice : StatusLevel::Error);

    const bool wifi_ok = initializeWiFi();
    overall_ok &= wifi_ok;

    const bool mqtt_ok = initializeMqttBridge();
    overall_ok &= mqtt_ok;

    const bool bridge_ok = initializeBridge();
    overall_ok &= bridge_ok;

    bool bridge_tasks_ok = false;
    if (bridge_ok) {
        bridge_tasks_ok = Bridge_CreateTasks(&bridge);
        if (!bridge_tasks_ok) {
            logger.log(LOG_ERROR, "[BRIDGE] Task creation failed");
            overall_ok = false;
            publishStatusIfPossible("Bridge tasks unavailable", StatusLevel::Error);
        } else {
            publishStatusIfPossible("Bridge tasks running", StatusLevel::Notice);
        }
    }

    const bool config_editor_ok = initializeConfigEditor();
    overall_ok &= config_editor_ok;

    const bool web_task_ok = initWebServerTask();
    const bool web_task_handle_ok = web_task_ok && (webServerTaskHandle != nullptr);
    if (web_task_ok && !web_task_handle_ok) {
        logger.log(LOG_ERROR, "[WEB] Web server task handle was not created");
    }
    overall_ok &= web_task_handle_ok;

    bool websocket_task_ok = createTask(
        "WebSocket",
        websocketTask,
        TASK_DEFAULT_STACK_SIZE,
        nullptr,
        TASK_NORMAL_PRIORITY,
        &websocketTaskHandle
    );
    overall_ok &= websocket_task_ok;

    bool watchdog_task_ok = createTask(
        "Watchdog",
        WatchdogManager::watchdogTask,
        2048,
        &Watchdog,
        TASK_NORMAL_PRIORITY,
        &watchdogTaskHandle
    );
    overall_ok &= watchdog_task_ok;

    feedWatchdogSafely();

    if (event_bus_ok) {
        publishStatusIfPossible(
            web_task_handle_ok ? "Web server task running" : "Web server task failed",
            web_task_handle_ok ? StatusLevel::Notice : StatusLevel::Error
        );
        publishStatusIfPossible(
            websocket_task_ok ? "WebSocket task running" : "WebSocket task failed",
            websocket_task_ok ? StatusLevel::Notice : StatusLevel::Error
        );
        publishStatusIfPossible(
            watchdog_task_ok ? "Watchdog task running" : "Watchdog task failed",
            watchdog_task_ok ? StatusLevel::Notice : StatusLevel::Error
        );
    }

    if (overall_ok) {
        logger.log(LOG_INFO, "[INIT] All subsystems initialized successfully ✓");
        publishStatusIfPossible("System initialization complete", StatusLevel::Notice);
    } else {
        logger.log(LOG_ERROR, "[INIT] One or more subsystems failed to initialize");
        publishStatusIfPossible("System initialization incomplete", StatusLevel::Error);
    }

    return overall_ok;
}
