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
#include "tinybms_victron_bridge.h"

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
 * @brief Error codes returned by the configuration editor
 */
enum class TinyBMSConfigError : uint8_t {
    None = 0,
    MutexUnavailable,
    RegisterNotFound,
    OutOfRange,
    Timeout,
    WriteFailed,
    BridgeUnavailable
};

/**
 * @brief Result returned by configuration write operations
 */
struct TinyBMSConfigResult {
    TinyBMSConfigError error = TinyBMSConfigError::None;
    String message;

    bool ok() const { return error == TinyBMSConfigError::None; }
};

const char* tinybmsConfigErrorToString(TinyBMSConfigError error);

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
    TinyBMSConfigEditor();

    void begin();
    String getRegistersJSON();
    bool readRegister(uint16_t address, uint16_t &value);
    TinyBMSConfigError writeRegister(uint16_t address, uint16_t value);
    uint8_t readAllRegisters();
    const TinyBMSConfigRegister *getRegister(uint16_t address) const;
    TinyBMSConfigResult writeConfig(const TinyBMS_Config &cfg);

private:
    static const uint8_t MAX_REGISTERS = 40;
    TinyBMSConfigRegister registers_[MAX_REGISTERS];
    uint8_t registers_count_;

    int8_t findRegisterIndex(uint16_t address) const;
    void addRegister(uint16_t address,
                     const String &description,
                     const String &unit,
                     uint16_t min_value,
                     uint16_t max_value,
                     const String &type,
                     const String &comment,
                     uint16_t default_value = 0);
    void addEnumRegister(uint16_t address,
                         const String &description,
                         const String &unit,
                         uint16_t min_value,
                         uint16_t max_value,
                         const String &type,
                         const String &comment,
                         const String enum_values[],
                         uint8_t enum_count,
                         uint16_t default_value = 0);
    void initializeRegisters();
};

#endif // TINYBMS_CONFIG_EDITOR_H
