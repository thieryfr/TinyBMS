/**
 * @file config_manager.h
 * @brief Configuration Manager with JSON file support and FreeRTOS
 * @version 1.1 - FreeRTOS with Config Mutex and LogLevel
 * 
 * Manages configuration from config.json in SPIFFS
 * Provides easy access to all settings with defaults
 * Protects access with configMutex
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Freertos.h>
#include "rtos_tasks.h"
#include "logger.h" // Ajout pour LogLevel

extern SemaphoreHandle_t configMutex;

class ConfigManager {
public:
    ConfigManager() : loaded_(false) {}
    
    /**
     * @brief Load configuration from SPIFFS
     * @param filename Path to config file (default: /config.json)
     * @return true if loaded successfully
     */
    bool begin(const char* filename = "/config.json") {
        filename_ = filename;
        
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("[CONFIG] ❌ Échec prise mutex config");
            return false;
        }

        if (!SPIFFS.begin(true)) {
            Serial.println("[CONFIG] SPIFFS mount failed!");
            xSemaphoreGive(configMutex);
            return false;
        }
        
        if (!SPIFFS.exists(filename_)) {
            Serial.printf("[CONFIG] File %s not found, using defaults\n", filename_);
            loaded_ = false;
            xSemaphoreGive(configMutex);
            return false;
        }
        
        File file = SPIFFS.open(filename_, "r");
        if (!file) {
            Serial.printf("[CONFIG] Failed to open %s\n", filename_);
            xSemaphoreGive(configMutex);
            return false;
        }
        
        // Parse JSON
        DynamicJsonDocument doc(4096);  // 4KB buffer
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) {
            Serial.printf("[CONFIG] JSON parse error: %s\n", error.c_str());
            xSemaphoreGive(configMutex);
            return false;
        }
        
        // Load all sections
        loadWiFiConfig(doc);
        loadHardwareConfig(doc);
        loadTinyBMSConfig(doc);
        loadVictronConfig(doc);
        loadCVLConfig(doc);
        loadWebServerConfig(doc);
        loadLoggingConfig(doc);
        loadAdvancedConfig(doc);
        
        loaded_ = true;
        Serial.println("[CONFIG] Configuration loaded successfully");
        printConfig();
        
        xSemaphoreGive(configMutex);
        return true;
    }
    
    /**
     * @brief Save current configuration to SPIFFS
     */
    bool save() {
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("[CONFIG] ❌ Échec prise mutex config");
            return false;
        }

        DynamicJsonDocument doc(4096);
        
        // Build JSON from current config
        saveWiFiConfig(doc);
        saveHardwareConfig(doc);
        saveTinyBMSConfig(doc);
        saveVictronConfig(doc);
        saveCVLConfig(doc);
        saveWebServerConfig(doc);
        saveLoggingConfig(doc);
        saveAdvancedConfig(doc);
        
        // Write to file
        File file = SPIFFS.open(filename_, "w");
        if (!file) {
            Serial.println("[CONFIG] Failed to open file for writing");
            xSemaphoreGive(configMutex);
            return false;
        }
        
        serializeJson(doc, file);
        file.close();
        
        Serial.println("[CONFIG] Configuration saved");
        xSemaphoreGive(configMutex);
        return true;
    }
    
    // ========================================================================
    // WIFI CONFIGURATION
    // ========================================================================
    
    struct WiFiConfig {
        String ssid = "YourSSID";
        String password = "YourPassword";
        bool ap_fallback_enabled = true;
        String ap_ssid = "TinyBMS-Bridge";
        String ap_password = "12345678";
        String hostname = "tinybms-bridge";
    } wifi;
    
    // ========================================================================
    // HARDWARE CONFIGURATION
    // ========================================================================
    
    struct HardwareConfig {
        struct UART {
            int rx_pin = 16;
            int tx_pin = 17;
            int baudrate = 115200;
            int timeout_ms = 1000;
        } uart;
        
        struct CAN {
            int rx_pin = 4;
            int tx_pin = 5;
            int bitrate = 250000;
            int mode = 0; // 0: Normal, 1: Listen only
        } can;
    } hardware;
    
    // ========================================================================
    // TINYBMS CONFIGURATION
    // ========================================================================
    
    struct TinyBMSConfig {
        int poll_interval_ms = 100;
        int uart_retry_count = 3;
        int uart_retry_delay_ms = 50;
        bool broadcast_expected = true;
    } tinybms;
    
    // ========================================================================
    // VICTRON CONFIGURATION
    // ========================================================================
    
    struct VictronConfig {
        int pgn_update_interval_ms = 1000;
        int cvl_update_interval_ms = 20000;
        int keepalive_timeout_ms = 5000;
        String manufacturer_name = "TinyBMS";
        String battery_name = "Lithium Battery";
    } victron;
    
    // ========================================================================
    // CVL ALGORITHM CONFIGURATION
    // ========================================================================
    
    struct CVLConfig {
        bool enabled = true;
        float bulk_soc_threshold = 90.0;
        float transition_soc_threshold = 95.0;
        float float_soc_threshold = 98.0;
        float float_exit_soc = 95.0;
        float float_approach_offset_mv = 50.0;
        float float_offset_mv = 100.0;
        float minimum_ccl_in_float_a = 5.0;
        int imbalance_hold_threshold_mv = 100;
        int imbalance_release_threshold_mv = 50;
    } cvl;
    
    // ========================================================================
    // WEB SERVER CONFIGURATION
    // ========================================================================
    
    struct WebServerConfig {
        int port = 80;
        int websocket_update_interval_ms = 1000;
        bool enable_cors = true;
        bool enable_auth = false;
        String username = "admin";
        String password = "admin";
    } web_server;
    
    // ========================================================================
    // LOGGING CONFIGURATION
    // ========================================================================
    
    struct LoggingConfig {
        int serial_baudrate = 115200;
        LogLevel log_level = LOG_INFO; // LogLevel de logger.h (0: ERROR, 1: WARNING, 2: INFO, 3: DEBUG)
        bool log_uart_traffic = false;
        bool log_can_traffic = false;
        bool log_cvl_changes = true;
    } logging;
    
    // ========================================================================
    // ADVANCED CONFIGURATION
    // ========================================================================
    
    struct AdvancedConfig {
        bool enable_spiffs = true;
        bool enable_ota = true;
        int watchdog_timeout_s = 5;
        int stack_size_bytes = 8192;
    } advanced;
    
    /**
     * @brief Check if configuration was loaded successfully
     */
    bool isLoaded() const {
        return loaded_;
    }

private:
    // Loading methods
    void loadWiFiConfig(JsonDocument& doc);
    void loadHardwareConfig(JsonDocument& doc);
    void loadTinyBMSConfig(JsonDocument& doc);
    void loadVictronConfig(JsonDocument& doc);
    void loadCVLConfig(JsonDocument& doc);
    void loadWebServerConfig(JsonDocument& doc);
    void loadLoggingConfig(JsonDocument& doc);
    void loadAdvancedConfig(JsonDocument& doc);
    
    // Saving methods
    void saveWiFiConfig(JsonDocument& doc);
    void saveHardwareConfig(JsonDocument& doc);
    void saveTinyBMSConfig(JsonDocument& doc);
    void saveVictronConfig(JsonDocument& doc);
    void saveCVLConfig(JsonDocument& doc);
    void saveWebServerConfig(JsonDocument& doc);
    void saveLoggingConfig(JsonDocument& doc);
    void saveAdvancedConfig(JsonDocument& doc);
    
    void printConfig() const;
};

#endif // CONFIG_MANAGER_H