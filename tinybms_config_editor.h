/**
 * @file tinybms_config_editor.h
 * @brief TinyBMS Configuration Editor Module with Logging + FreeRTOS
 * @version 1.1 - Integration with logger and structured feedback
 */

#ifndef TINYBMS_CONFIG_EDITOR_H
#define TINYBMS_CONFIG_EDITOR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Freertos.h>
#include "watchdog_manager.h"
#include "rtos_tasks.h"

extern WatchdogManager Watchdog;

// ✅ Optional logger integration
#ifdef LOGGER_AVAILABLE
#include "logger.h"
extern Logger logger;
#define CONFIG_LOG(level, msg) logger.log(level, String("[CONFIG_EDITOR] ") + msg)
#else
#define CONFIG_LOG(level, msg) Serial.println(String("[CONFIG_EDITOR] ") + msg)
#endif

/**
 * @brief TinyBMS Configuration Register Definition
 */
struct TinyBMSConfigRegister {
    uint16_t address;
    String description;
    String unit;
    uint16_t min_value;
    uint16_t max_value;
    uint16_t current_value;
    String type;
    String comment;
    bool is_enum;
    String enum_values[10];
    uint8_t enum_count;
};

/**
 * @brief TinyBMS Configuration Editor Class
 */
class TinyBMSConfigEditor {
public:
    TinyBMSConfigEditor() : registers_count_(0) {}

    // -------------------------------------------------------------------------
    // INITIALIZATION
    // -------------------------------------------------------------------------
    void begin() {
        CONFIG_LOG(LOG_INFO, "Initializing TinyBMS configuration registers...");
        initializeRegisters();
        CONFIG_LOG(LOG_INFO, "Loaded " + String(registers_count_) + " configuration registers");
    }

    // -------------------------------------------------------------------------
    // JSON EXPORT
    // -------------------------------------------------------------------------
    String getRegistersJSON() {
        DynamicJsonDocument doc(8192);
        JsonArray regs = doc.createNestedArray("registers");

        for (uint8_t i = 0; i < registers_count_; i++) {
            JsonObject reg = regs.createNestedObject();
            reg["address"] = registers_[i].address;
            reg["description"] = registers_[i].description;
            reg["unit"] = registers_[i].unit;
            reg["min"] = registers_[i].min_value;
            reg["max"] = registers_[i].max_value;
            reg["value"] = registers_[i].current_value;
            reg["type"] = registers_[i].type;
            reg["comment"] = registers_[i].comment;
            reg["is_enum"] = registers_[i].is_enum;

            if (registers_[i].is_enum) {
                JsonArray enums = reg.createNestedArray("enum_values");
                for (uint8_t j = 0; j < registers_[i].enum_count; j++) {
                    enums.add(registers_[i].enum_values[j]);
                }
            }
        }

        String output;
        serializeJson(doc, output);
        CONFIG_LOG(LOG_DEBUG, "Built JSON with " + String(registers_count_) + " registers");
        return output;
    }

    // -------------------------------------------------------------------------
    // READ REGISTER
    // -------------------------------------------------------------------------
    bool readRegister(uint16_t address, uint16_t &value) {
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            CONFIG_LOG(LOG_ERROR, "❌ UART mutex unavailable");
            return false;
        }

        int8_t idx = findRegisterIndex(address);
        if (idx < 0) {
            CONFIG_LOG(LOG_WARN, "Register " + String(address) + " not found");
            xSemaphoreGive(uartMutex);
            return false;
        }

        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":0001%02X\r\n", address % 256);
        Serial2.write(cmd);
        CONFIG_LOG(LOG_DEBUG, "Read request sent for register " + String(address));

        uint32_t start = millis();
        String response = "";
        while (millis() - start < 1000) {
            if (Serial2.available()) {
                char c = Serial2.read();
                response += c;

                if (c == '\n' && response.startsWith(":") && response.length() >= 8) {
                    String hex_value = response.substring(3, 7);
                    value = strtol(hex_value.c_str(), NULL, 16);
                    registers_[idx].current_value = value;

                    CONFIG_LOG(LOG_INFO, "Reg " + String(address) + " → " +
                                         String(value) + " (0x" + String(value, HEX) + ")");
                    xSemaphoreGive(uartMutex);
                    return true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        CONFIG_LOG(LOG_WARN, "Timeout reading register " + String(address));
        xSemaphoreGive(uartMutex);
        return false;
    }

    // -------------------------------------------------------------------------
    // WRITE REGISTER
    // -------------------------------------------------------------------------
    bool writeRegister(uint16_t address, uint16_t value) {
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            CONFIG_LOG(LOG_ERROR, "❌ UART mutex unavailable");
            return false;
        }

        int8_t idx = findRegisterIndex(address);
        if (idx < 0) {
            CONFIG_LOG(LOG_WARN, "Register " + String(address) + " not found");
            xSemaphoreGive(uartMutex);
            return false;
        }

        if (value < registers_[idx].min_value || value > registers_[idx].max_value) {
            CONFIG_LOG(LOG_WARN, "Value " + String(value) + " out of range [" +
                                   String(registers_[idx].min_value) + "-" +
                                   String(registers_[idx].max_value) + "]");
            xSemaphoreGive(uartMutex);
            return false;
        }

        char cmd[20];
        snprintf(cmd, sizeof(cmd), ":0101%02X%04X\r\n", address % 256, value);
        Serial2.write(cmd);
        CONFIG_LOG(LOG_DEBUG, "Write request " + String(address) + " = " + String(value));

        uint32_t start = millis();
        String response = "";

        while (millis() - start < 1000) {
            if (Serial2.available()) {
                char c = Serial2.read();
                response += c;

                if (c == '\n') {
                    if (response.indexOf(":OK") >= 0 || response.indexOf("ACK") >= 0) {
                        registers_[idx].current_value = value;
                        CONFIG_LOG(LOG_INFO, "Write OK → Reg " + String(address) +
                                             " = " + String(value));
                        xSemaphoreGive(uartMutex);
                        return true;
                    } else {
                        CONFIG_LOG(LOG_ERROR, "Write failed for " + String(address) +
                                              " → " + response);
                        xSemaphoreGive(uartMutex);
                        return false;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        CONFIG_LOG(LOG_WARN, "Write timeout for register " + String(address));
        xSemaphoreGive(uartMutex);
        return false;
    }

    // -------------------------------------------------------------------------
    // READ ALL REGISTERS
    // -------------------------------------------------------------------------
    uint8_t readAllRegisters() {
        CONFIG_LOG(LOG_INFO, "Reading all configuration registers...");
        uint8_t success_count = 0;

        for (uint8_t i = 0; i < registers_count_; i++) {
            uint16_t value;
            if (readRegister(registers_[i].address, value)) {
                success_count++;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        CONFIG_LOG(LOG_INFO, "Read " + String(success_count) + "/" +
                             String(registers_count_) + " registers successfully");
        return success_count;
    }

    // -------------------------------------------------------------------------
    // ACCESSOR
    // -------------------------------------------------------------------------
    const TinyBMSConfigRegister *getRegister(uint16_t address) {
        int8_t idx = findRegisterIndex(address);
        return (idx >= 0) ? &registers_[idx] : nullptr;
    }

private:
    static const uint8_t MAX_REGISTERS = 40;
    TinyBMSConfigRegister registers_[MAX_REGISTERS];
    uint8_t registers_count_;

    int8_t findRegisterIndex(uint16_t address) {
        for (uint8_t i = 0; i < registers_count_; i++) {
            if (registers_[i].address == address) return i;
        }
        return -1;
    }

    // AddRegister and addEnumRegister remain unchanged...
    // initializeRegisters() remains identical except for optional CONFIG_LOG on completion.
    void initializeRegisters(); // defined below or elsewhere
};

#endif // TINYBMS_CONFIG_EDITOR_H