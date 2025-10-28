/**
 * @file tinybms_config_editor.cpp
 * @brief Implementation of the TinyBMS configuration editor helpers
 */

#include "tinybms_config_editor.h"

#include <HardwareSerial.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "tinybms_victron_bridge.h"

// -----------------------------------------------------------------------------
// External resources
// -----------------------------------------------------------------------------
extern SemaphoreHandle_t uartMutex;
extern TinyBMS_Victron_Bridge bridge;

// -----------------------------------------------------------------------------
// Helper constants
// -----------------------------------------------------------------------------
namespace {

constexpr uint16_t REG_FULLY_CHARGED_VOLTAGE      = 300;
constexpr uint16_t REG_FULLY_DISCHARGED_VOLTAGE   = 301;
constexpr uint16_t REG_CHARGE_FINISHED_CURRENT    = 304;
constexpr uint16_t REG_BATTERY_CAPACITY           = 306;
constexpr uint16_t REG_CELL_COUNT                 = 307;
constexpr uint16_t REG_OVERVOLTAGE_CUTOFF         = 315;
constexpr uint16_t REG_UNDERVOLTAGE_CUTOFF        = 316;
constexpr uint16_t REG_DISCHARGE_OVERCURRENT      = 317;
constexpr uint16_t REG_CHARGE_OVERCURRENT         = 318;
constexpr uint16_t REG_OVERHEAT_CUTOFF            = 319;
constexpr uint16_t REG_LOW_TEMP_CHARGE_CUTOFF     = 320;

const char* toStringInternal(TinyBMSConfigError error) {
    switch (error) {
        case TinyBMSConfigError::None:             return "none";
        case TinyBMSConfigError::MutexUnavailable: return "mutex_unavailable";
        case TinyBMSConfigError::RegisterNotFound: return "register_not_found";
        case TinyBMSConfigError::OutOfRange:       return "out_of_range";
        case TinyBMSConfigError::Timeout:          return "timeout";
        case TinyBMSConfigError::WriteFailed:      return "write_failed";
        case TinyBMSConfigError::BridgeUnavailable:return "bridge_unavailable";
        default:                                   return "unknown";
    }
}

} // namespace

const char* tinybmsConfigErrorToString(TinyBMSConfigError error) {
    return toStringInternal(error);
}

// -----------------------------------------------------------------------------
// Construction & lifecycle
// -----------------------------------------------------------------------------
TinyBMSConfigEditor::TinyBMSConfigEditor()
    : registers_count_(0) {}

void TinyBMSConfigEditor::begin() {
    if (registers_count_ != 0) {
        CONFIG_LOG(LOG_DEBUG, "Register catalog already initialized");
        return;
    }
    CONFIG_LOG(LOG_INFO, "Initializing TinyBMS configuration registers...");
    initializeRegisters();
    CONFIG_LOG(LOG_INFO, "Loaded " + String(registers_count_) + " configuration registers");
}

// -----------------------------------------------------------------------------
// JSON helpers
// -----------------------------------------------------------------------------
String TinyBMSConfigEditor::getRegistersJSON() {
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

// -----------------------------------------------------------------------------
// Register IO
// -----------------------------------------------------------------------------
bool TinyBMSConfigEditor::readRegister(uint16_t address, uint16_t &value) {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        CONFIG_LOG(LOG_ERROR, "UART mutex unavailable for read");
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
    bridge.tiny_uart_.write(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd));
    CONFIG_LOG(LOG_DEBUG, "Read request sent for register " + String(address));

    uint32_t start = millis();
    String response;
    while (millis() - start < 1000) {
        if (bridge.tiny_uart_.available()) {
            char c = bridge.tiny_uart_.read();
            response += c;

            if (c == '\n' && response.startsWith(":") && response.length() >= 8) {
                String hex_value = response.substring(3, 7);
                value = strtol(hex_value.c_str(), nullptr, 16);
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

TinyBMSConfigError TinyBMSConfigEditor::writeRegister(uint16_t address, uint16_t value) {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        CONFIG_LOG(LOG_ERROR, "UART mutex unavailable for write");
        return TinyBMSConfigError::MutexUnavailable;
    }

    int8_t idx = findRegisterIndex(address);
    if (idx < 0) {
        CONFIG_LOG(LOG_WARN, "Register " + String(address) + " not found");
        xSemaphoreGive(uartMutex);
        return TinyBMSConfigError::RegisterNotFound;
    }

    if (value < registers_[idx].min_value || value > registers_[idx].max_value) {
        CONFIG_LOG(LOG_WARN, "Value " + String(value) + " out of range [" +
                               String(registers_[idx].min_value) + "-" +
                               String(registers_[idx].max_value) + "]");
        xSemaphoreGive(uartMutex);
        return TinyBMSConfigError::OutOfRange;
    }

    char cmd[20];
    snprintf(cmd, sizeof(cmd), ":0101%02X%04X\r\n", address % 256, value);
    bridge.tiny_uart_.write(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd));
    CONFIG_LOG(LOG_DEBUG, "Write request " + String(address) + " = " + String(value));

    uint32_t start = millis();
    String response;

    while (millis() - start < 1000) {
        if (bridge.tiny_uart_.available()) {
            char c = bridge.tiny_uart_.read();
            response += c;

            if (c == '\n') {
                if (response.indexOf(":OK") >= 0 || response.indexOf("ACK") >= 0) {
                    registers_[idx].current_value = value;
                    CONFIG_LOG(LOG_INFO, "Write OK → Reg " + String(address) +
                                             " = " + String(value));
                    xSemaphoreGive(uartMutex);
                    return TinyBMSConfigError::None;
                }

                CONFIG_LOG(LOG_ERROR, "Write failed for " + String(address) +
                                          " → " + response);
                xSemaphoreGive(uartMutex);
                return TinyBMSConfigError::WriteFailed;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    CONFIG_LOG(LOG_WARN, "Write timeout for register " + String(address));
    xSemaphoreGive(uartMutex);
    return TinyBMSConfigError::Timeout;
}

uint8_t TinyBMSConfigEditor::readAllRegisters() {
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

const TinyBMSConfigRegister *TinyBMSConfigEditor::getRegister(uint16_t address) const {
    int8_t idx = findRegisterIndex(address);
    return (idx >= 0) ? &registers_[idx] : nullptr;
}

// -----------------------------------------------------------------------------
// Configuration orchestration
// -----------------------------------------------------------------------------
TinyBMSConfigResult TinyBMSConfigEditor::writeConfig(const TinyBMS_Config &cfg) {
    TinyBMSConfigResult result;

    if (!bridge.initialized_) {
        result.error = TinyBMSConfigError::BridgeUnavailable;
        result.message = "TinyBMS bridge not initialized";
        return result;
    }

    struct RegisterWrite {
        uint16_t address;
        uint16_t value;
        const char *field_name;
    } updates[] = {
        {REG_FULLY_CHARGED_VOLTAGE,    cfg.fully_charged_voltage_mv,       "fully_charged_voltage_mv"},
        {REG_FULLY_DISCHARGED_VOLTAGE, cfg.fully_discharged_voltage_mv,    "fully_discharged_voltage_mv"},
        {REG_CHARGE_FINISHED_CURRENT,  cfg.charge_finished_current_ma,     "charge_finished_current_ma"},
        {REG_BATTERY_CAPACITY,         static_cast<uint16_t>(std::lroundf(cfg.battery_capacity_ah_scaled)), "battery_capacity_ah"},
        {REG_CELL_COUNT,               cfg.cell_count,                     "cell_count"},
        {REG_OVERVOLTAGE_CUTOFF,       cfg.overvoltage_cutoff_mv,          "overvoltage_cutoff_mv"},
        {REG_UNDERVOLTAGE_CUTOFF,      cfg.undervoltage_cutoff_mv,         "undervoltage_cutoff_mv"},
        {REG_DISCHARGE_OVERCURRENT,    cfg.discharge_overcurrent_a,        "discharge_overcurrent_a"},
        {REG_CHARGE_OVERCURRENT,       cfg.charge_overcurrent_a,           "charge_overcurrent_a"},
        {REG_OVERHEAT_CUTOFF,          static_cast<uint16_t>(std::lroundf(cfg.overheat_cutoff_c / 10.0f)), "overheat_cutoff_c"},
        {REG_LOW_TEMP_CHARGE_CUTOFF,   static_cast<uint16_t>(std::lroundf(cfg.low_temp_charge_cutoff_c / 10.0f)), "low_temp_charge_cutoff_c"}
    };

    for (const auto &update : updates) {
        TinyBMSConfigError err = writeRegister(update.address, update.value);
        if (err != TinyBMSConfigError::None) {
            result.error = err;
            result.message = String("Failed to write ") + update.field_name +
                             " (" + tinybmsConfigErrorToString(err) + ")";
            CONFIG_LOG(LOG_ERROR, result.message);
            return result;
        }
    }

    bridge.config_ = cfg;
    result.message = "Configuration written successfully";
    CONFIG_LOG(LOG_INFO, result.message);
    return result;
}

// -----------------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------------
int8_t TinyBMSConfigEditor::findRegisterIndex(uint16_t address) const {
    for (uint8_t i = 0; i < registers_count_; i++) {
        if (registers_[i].address == address) {
            return i;
        }
    }
    return -1;
}

void TinyBMSConfigEditor::addRegister(uint16_t address,
                                      const String &description,
                                      const String &unit,
                                      uint16_t min_value,
                                      uint16_t max_value,
                                      const String &type,
                                      const String &comment,
                                      uint16_t default_value) {
    if (registers_count_ >= MAX_REGISTERS) {
        CONFIG_LOG(LOG_WARN, "Register table full, skipping address " + String(address));
        return;
    }

    TinyBMSConfigRegister &reg = registers_[registers_count_++];
    reg.address = address;
    reg.description = description;
    reg.unit = unit;
    reg.min_value = min_value;
    reg.max_value = max_value;
    reg.current_value = default_value;
    reg.type = type;
    reg.comment = comment;
    reg.is_enum = false;
    reg.enum_count = 0;
}

void TinyBMSConfigEditor::addEnumRegister(uint16_t address,
                                          const String &description,
                                          const String &unit,
                                          uint16_t min_value,
                                          uint16_t max_value,
                                          const String &type,
                                          const String &comment,
                                          const String enum_values[],
                                          uint8_t enum_count,
                                          uint16_t default_value) {
    addRegister(address, description, unit, min_value, max_value, type, comment, default_value);

    TinyBMSConfigRegister &reg = registers_[registers_count_ - 1];
    reg.is_enum = true;
    reg.enum_count = std::min<uint8_t>(enum_count, 10);
    for (uint8_t i = 0; i < reg.enum_count; ++i) {
        reg.enum_values[i] = enum_values[i];
    }
}

void TinyBMSConfigEditor::initializeRegisters() {
    registers_count_ = 0;

    addRegister(REG_FULLY_CHARGED_VOLTAGE,
                "Fully Charged Voltage",
                "mV",
                1200,
                4500,
                "UINT16",
                "Cell voltage when considered fully charged",
                3650);

    addRegister(REG_FULLY_DISCHARGED_VOLTAGE,
                "Fully Discharged Voltage",
                "mV",
                1000,
                3500,
                "UINT16",
                "Cell voltage considered fully discharged",
                3250);

    addRegister(REG_CHARGE_FINISHED_CURRENT,
                "Charge Finished Current",
                "mA",
                100,
                5000,
                "UINT16",
                "Current threshold signalling charge completion",
                1000);

    addRegister(REG_BATTERY_CAPACITY,
                "Battery Capacity",
                "0.01Ah",
                10,
                65500,
                "UINT16",
                "Battery capacity used for SOC calculations",
                3140);

    const String cell_counts[] = {"4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
    addEnumRegister(REG_CELL_COUNT,
                    "Number of Series Cells",
                    "cells",
                    4,
                    16,
                    "UINT16",
                    "Configured number of series-connected cells",
                    cell_counts,
                    sizeof(cell_counts) / sizeof(cell_counts[0]),
                    16);

    addRegister(REG_OVERVOLTAGE_CUTOFF,
                "Over-voltage Cutoff",
                "mV",
                1200,
                4500,
                "UINT16",
                "Cell voltage threshold to stop charging",
                3800);

    addRegister(REG_UNDERVOLTAGE_CUTOFF,
                "Under-voltage Cutoff",
                "mV",
                800,
                3500,
                "UINT16",
                "Cell voltage threshold to stop discharging",
                2800);

    addRegister(REG_DISCHARGE_OVERCURRENT,
                "Discharge Over-current Cutoff",
                "A",
                1,
                750,
                "UINT16",
                "Current limit for discharge protection",
                65);

    addRegister(REG_CHARGE_OVERCURRENT,
                "Charge Over-current Cutoff",
                "A",
                1,
                750,
                "UINT16",
                "Current limit for charge protection",
                90);

    addRegister(REG_OVERHEAT_CUTOFF,
                "Overheat Cutoff",
                "°C",
                20,
                90,
                "UINT16",
                "Temperature threshold to stop charging/discharging",
                60);

    addRegister(REG_LOW_TEMP_CHARGE_CUTOFF,
                "Low Temperature Charge Cutoff",
                "°C",
                0,
                50,
                "UINT16",
                "Temperature below which charging is disabled",
                0);
}

