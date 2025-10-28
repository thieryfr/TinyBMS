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

namespace {

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

const char* accessToString(TinyRegisterAccess access) {
    switch (access) {
        case TinyRegisterAccess::ReadOnly:  return "ro";
        case TinyRegisterAccess::WriteOnly: return "wo";
        case TinyRegisterAccess::ReadWrite: default: return "rw";
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
    DynamicJsonDocument doc(12288);
    doc["success"] = true;
    doc["count"] = registers_count_;
    JsonArray regs = doc.createNestedArray("registers");

    for (uint8_t i = 0; i < registers_count_; i++) {
        JsonObject reg = regs.createNestedObject();
        reg["address"] = registers_[i].address;
        reg["key"] = registers_[i].key;
        reg["label"] = registers_[i].description;
        reg["group"] = registers_[i].group;
        reg["unit"] = registers_[i].unit;
        reg["type"] = registers_[i].type;
        reg["comment"] = registers_[i].comment;
        reg["access"] = accessToString(registers_[i].access);
        reg["scale"] = registers_[i].scale;
        reg["offset"] = registers_[i].offset;
        reg["step"] = registers_[i].step;
        reg["precision"] = registers_[i].precision;
        reg["default"] = registers_[i].default_user_value;
        reg["raw_default"] = registers_[i].default_raw_value;
        reg["value"] = registers_[i].current_user_value;
        reg["raw_value"] = registers_[i].current_raw_value;
        reg["is_enum"] = registers_[i].is_enum;

        if (registers_[i].has_min) {
            reg["min"] = registers_[i].min_value;
        }
        if (registers_[i].has_max) {
            reg["max"] = registers_[i].max_value;
        }

        if (registers_[i].is_enum) {
            JsonArray enums = reg.createNestedArray("enum_values");
            for (uint8_t j = 0; j < registers_[i].enum_count; j++) {
                JsonObject entry = enums.createNestedObject();
                entry["value"] = registers_[i].enum_values[j].value;
                entry["label"] = registers_[i].enum_values[j].label;
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
bool TinyBMSConfigEditor::readRegister(uint16_t address, float &value) {
    uint16_t raw_value = 0;
    if (!readRegisterRaw(address, raw_value)) {
        return false;
    }

    int8_t idx = findRegisterIndex(address);
    if (idx >= 0) {
        value = registers_[idx].current_user_value;
    } else {
        value = 0.0f;
    }
    return true;
}

bool TinyBMSConfigEditor::readRegisterRaw(uint16_t address, uint16_t &value) {
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

    TinyBMSConfigRegister &reg = registers_[idx];

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
                value = static_cast<uint16_t>(strtol(hex_value.c_str(), nullptr, 16));
                float user_value = convertRawToUser(reg, value);
                reg.current_raw_value = value;
                reg.current_user_value = user_value;

                String user_str = String(user_value, reg.precision);
                String log_message = "Reg " + String(address) + " → " + user_str;
                if (!reg.unit.isEmpty()) {
                    log_message += " " + reg.unit;
                }
                log_message += " (raw=0x" + String(value, HEX) + ")";
                CONFIG_LOG(LOG_INFO, log_message);
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

TinyBMSConfigError TinyBMSConfigEditor::writeRegister(uint16_t address, float user_value) {
    int8_t idx = findRegisterIndex(address);
    if (idx < 0) {
        return TinyBMSConfigError::RegisterNotFound;
    }

    TinyBMSConfigRegister &reg = registers_[idx];
    TinyBMSConfigError validation = validateValue(reg, user_value);
    if (validation != TinyBMSConfigError::None) {
        return validation;
    }

    uint16_t raw_value = 0;
    if (!convertUserToRaw(reg, user_value, raw_value)) {
        return TinyBMSConfigError::OutOfRange;
    }

    return writeRegisterRaw(address, raw_value);
}

TinyBMSConfigError TinyBMSConfigEditor::writeRegisterRaw(uint16_t address, uint16_t value) {
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

    TinyBMSConfigRegister &reg = registers_[idx];

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
                    float user_value = convertRawToUser(reg, value);
                    reg.current_raw_value = value;
                    reg.current_user_value = user_value;

                    String log_message = "Write OK → Reg " + String(address) + " = " +
                                         String(user_value, reg.precision);
                    if (!reg.unit.isEmpty()) {
                        log_message += " " + reg.unit;
                    }
                    log_message += " (raw=" + String(value) + ")";
                    CONFIG_LOG(LOG_INFO, log_message);
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
        float value = 0.0f;
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

    struct ConfigBinding {
        const char* key;
        float value;
    } bindings[] = {
        {"fully_charged_voltage_mv", static_cast<float>(cfg.fully_charged_voltage_mv)},
        {"fully_discharged_voltage_mv", static_cast<float>(cfg.fully_discharged_voltage_mv)},
        {"charge_finished_current_ma", static_cast<float>(cfg.charge_finished_current_ma)},
        {"battery_capacity_ah", cfg.battery_capacity_ah},
        {"cell_count", static_cast<float>(cfg.cell_count)},
        {"overvoltage_cutoff_mv", static_cast<float>(cfg.overvoltage_cutoff_mv)},
        {"undervoltage_cutoff_mv", static_cast<float>(cfg.undervoltage_cutoff_mv)},
        {"discharge_overcurrent_a", static_cast<float>(cfg.discharge_overcurrent_a)},
        {"charge_overcurrent_a", static_cast<float>(cfg.charge_overcurrent_a)},
        {"overheat_cutoff_c", cfg.overheat_cutoff_c},
        {"low_temp_charge_cutoff_c", cfg.low_temp_charge_cutoff_c}
    };

    for (const auto &binding : bindings) {
        int8_t idx = findRegisterIndexByKey(String(binding.key));
        if (idx < 0) {
            CONFIG_LOG(LOG_DEBUG, String("Skipping config field '") + binding.key + "' (no register)");
            continue;
        }

        TinyBMSConfigError err = writeRegister(registers_[idx].address, binding.value);
        if (err != TinyBMSConfigError::None) {
            result.error = err;
            result.message = String("Failed to write ") + binding.key +
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

void TinyBMSConfigEditor::initializeRegisters() {
    registers_count_ = 0;
    const auto &metadata = getTinyRwRegisters();
    if (metadata.empty()) {
        CONFIG_LOG(LOG_ERROR, "[CONFIG_EDITOR] tiny_rw_bms mapping unavailable");
        return;
    }

    for (const auto &meta : metadata) {
        if (registers_count_ >= MAX_REGISTERS) {
            CONFIG_LOG(LOG_WARN, "Register catalog full, skipping " + String(meta.address));
            break;
        }

        TinyBMSConfigRegister &reg = registers_[registers_count_++];
        reg.address = meta.address;
        reg.key = meta.key;
        reg.description = meta.label;
        reg.group = meta.group;
        reg.unit = meta.unit;
        reg.type = meta.type;
        reg.comment = meta.comment;
        reg.access = meta.access;
        reg.value_class = meta.value_class;
        reg.has_min = meta.has_min;
        reg.min_value = meta.min_value;
        reg.has_max = meta.has_max;
        reg.max_value = meta.max_value;
        reg.scale = meta.scale;
        reg.offset = meta.offset;
        reg.step = meta.step;
        reg.precision = meta.precision;
        reg.default_raw_value = meta.default_raw;
        reg.default_user_value = meta.default_value;
        reg.current_raw_value = meta.default_raw;
        reg.current_user_value = meta.default_value;
        reg.is_enum = !meta.enum_values.empty();
        reg.enum_count = std::min<uint8_t>(meta.enum_values.size(), static_cast<uint8_t>(sizeof(reg.enum_values) / sizeof(reg.enum_values[0])));
        for (uint8_t i = 0; i < reg.enum_count; ++i) {
            reg.enum_values[i].value = meta.enum_values[i].value;
            reg.enum_values[i].label = meta.enum_values[i].label;
        }
    }
}

int8_t TinyBMSConfigEditor::findRegisterIndexByKey(const String &key) const {
    if (key.isEmpty()) {
        return -1;
    }

    for (uint8_t i = 0; i < registers_count_; ++i) {
        if (registers_[i].key == key) {
            return i;
        }
    }
    return -1;
}

bool TinyBMSConfigEditor::convertUserToRaw(const TinyBMSConfigRegister &reg, float user_value, uint16_t &raw) const {
    if (reg.is_enum) {
        uint16_t candidate = static_cast<uint16_t>(std::lround(user_value));
        if (reg.enum_count > 0) {
            bool found = false;
            for (uint8_t i = 0; i < reg.enum_count; ++i) {
                if (reg.enum_values[i].value == candidate) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        raw = candidate;
        return true;
    }

    float denominator = std::abs(reg.scale) < 1e-6f ? 1.0f : reg.scale;
    float scaled = (user_value - reg.offset) / denominator;

    if (reg.value_class == TinyRegisterValueClass::Int) {
        long candidate = std::lround(scaled);
        if (candidate < -32768 || candidate > 32767) {
            return false;
        }
        raw = static_cast<uint16_t>(static_cast<int16_t>(candidate));
        return true;
    }

    long candidate = std::lround(scaled);
    if (candidate < 0 || candidate > 65535) {
        return false;
    }
    raw = static_cast<uint16_t>(candidate);
    return true;
}

float TinyBMSConfigEditor::convertRawToUser(const TinyBMSConfigRegister &reg, uint16_t raw) const {
    float base = static_cast<float>(raw);
    if (reg.value_class == TinyRegisterValueClass::Int) {
        base = static_cast<float>(static_cast<int16_t>(raw));
    }
    return base * reg.scale + reg.offset;
}

TinyBMSConfigError TinyBMSConfigEditor::validateValue(const TinyBMSConfigRegister &reg, float user_value) const {
    if (reg.is_enum) {
        uint16_t candidate = static_cast<uint16_t>(std::lround(user_value));
        if (reg.enum_count > 0) {
            for (uint8_t i = 0; i < reg.enum_count; ++i) {
                if (reg.enum_values[i].value == candidate) {
                    return TinyBMSConfigError::None;
                }
            }
            CONFIG_LOG(LOG_WARN, "Enum value " + String(candidate) + " not allowed for register " + String(reg.address));
            return TinyBMSConfigError::OutOfRange;
        }
        return TinyBMSConfigError::None;
    }

    if (reg.has_min && user_value < reg.min_value - 0.0001f) {
        CONFIG_LOG(LOG_WARN, "Value " + String(user_value, reg.precision) + " below minimum " + String(reg.min_value, reg.precision) + " for register " + String(reg.address));
        return TinyBMSConfigError::OutOfRange;
    }
    if (reg.has_max && user_value > reg.max_value + 0.0001f) {
        CONFIG_LOG(LOG_WARN, "Value " + String(user_value, reg.precision) + " above maximum " + String(reg.max_value, reg.precision) + " for register " + String(reg.address));
        return TinyBMSConfigError::OutOfRange;
    }

    return TinyBMSConfigError::None;
}

