/**
 * @file tinybms_victron_bridge.h
 * @brief TinyBMS UART to Victron CAN-BMS Bridge with FreeRTOS and Logging Support
 * @version 2.1 - Logging Integration
 * @date 2025-10
 * 
 * Defines data structures, tasks, and CAN/UART mapping for TinyBMS ↔ Victron bridge.
 */

#ifndef TINYBMS_VICTRON_BRIDGE_H
#define TINYBMS_VICTRON_BRIDGE_H

#include <Arduino.h>
#include <CAN.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "shared_data.h"

// ====================================================================================
// LOGGER SUPPORT (optional)
// ====================================================================================
#ifdef LOGGER_AVAILABLE
#include "logger.h"
extern Logger logger;
#define BRIDGE_LOG(level, msg) logger.log(level, String("[BRIDGE] ") + msg)
#else
#define BRIDGE_LOG(level, msg) Serial.println(String("[BRIDGE] ") + msg)
#endif

// ====================================================================================
// HARDWARE CONFIGURATION
// ====================================================================================
#define TINYBMS_UART_RX        16
#define TINYBMS_UART_TX        17
#define TINYBMS_UART_BAUD      115200

#define VICTRON_CAN_TX         5
#define VICTRON_CAN_RX         4
#define VICTRON_CAN_BITRATE    500000  // 500 kbps

// ====================================================================================
// TIMING CONFIGURATION
// ====================================================================================
#define PGN_UPDATE_INTERVAL_MS     1000   // 1 Hz for critical PGNs
#define CVL_UPDATE_INTERVAL_MS     20000  // 20s for CVL algorithm
#define UART_POLL_INTERVAL_MS      100    // 10 Hz UART polling
#define VICTRON_KEEPALIVE_TIMEOUT  10000  // 10s timeout

// ====================================================================================
// TINYBMS REGISTERS
// ====================================================================================
#define TINY_REG_VOLTAGE       36
#define TINY_REG_CURRENT       38
#define TINY_REG_MIN_CELL      40
#define TINY_REG_MAX_CELL      41
#define TINY_REG_SOH           45
#define TINY_REG_SOC           46
#define TINY_REG_TEMP_INTERNAL 48
#define TINY_REG_ONLINE_STATUS 50
#define TINY_REG_BALANCING     52
#define TINY_REG_MAX_DISCHARGE 102
#define TINY_REG_MAX_CHARGE    103

#define TINY_REG_FULLY_CHARGED     300
#define TINY_REG_CHARGE_FINISHED   304
#define TINY_REG_BATTERY_CAPACITY  306
#define TINY_REG_CELL_COUNT        307
#define TINY_REG_OVERVOLTAGE       315
#define TINY_REG_UNDERVOLTAGE      316
#define TINY_REG_DISCHARGE_OC      317
#define TINY_REG_CHARGE_OC         318
#define TINY_REG_OVERHEAT          319
#define TINY_REG_LOW_TEMP_CHARGE   320

// ====================================================================================
// VICTRON PGN IDS
// ====================================================================================
#define VICTRON_PGN_CVL_CCL_DCL     0x351
#define VICTRON_PGN_SOC_SOH         0x355
#define VICTRON_PGN_VOLTAGE_CURRENT 0x356
#define VICTRON_PGN_ALARMS          0x35A
#define VICTRON_PGN_MANUFACTURER    0x35E
#define VICTRON_PGN_BATTERY_INFO    0x35F
#define VICTRON_PGN_NAME_1          0x370
#define VICTRON_PGN_NAME_2          0x371
#define VICTRON_PGN_ENERGY          0x378
#define VICTRON_PGN_CAPACITY        0x379
#define VICTRON_PGN_KEEPALIVE       0x305
#define VICTRON_PGN_INVERTER_ID     0x307

// ====================================================================================
// CONFIGURATION STRUCTURES
// ====================================================================================

/**
 * @brief TinyBMS configuration registers
 */
struct TinyBMS_Config {
    uint16_t fully_charged_voltage_mv;
    uint16_t fully_discharged_voltage_mv;
    uint16_t charge_finished_current_ma;
    uint16_t battery_capacity_ah_scaled; // Scale 0.01Ah
    uint8_t  cell_count;
    uint16_t overvoltage_cutoff_mv;
    uint16_t undervoltage_cutoff_mv;
    uint16_t discharge_overcurrent_a;
    uint16_t charge_overcurrent_a;
    int16_t  overheat_cutoff_c;
    int16_t  low_temp_charge_cutoff_c;
};

/**
 * @brief CVL Algorithm States
 */
enum CVL_State {
    CVL_BULK_ABSORPTION,   // SOC < 90%
    CVL_TRANSITION,        // 90% <= SOC < 95%
    CVL_FLOAT_APPROACH,    // 95% <= SOC < 100%
    CVL_FLOAT,             // SOC >= 100% or MaxCell >= FullyCharged
    CVL_IMBALANCE_HOLD     // Cell imbalance > threshold
};

// ====================================================================================
// MAIN BRIDGE CLASS
// ====================================================================================

/**
 * @class TinyBMS_Victron_Bridge
 * @brief Bridge entre TinyBMS (UART) et Victron (CAN-BUS)
 */
class TinyBMS_Victron_Bridge {
public:
    TinyBMS_Victron_Bridge();

    /** @brief Initialize UART/CAN and start bridge */
    bool begin();

    /** @brief Get latest live data */
    const TinyBMS_LiveData& getLiveData() const { return live_data_; }

    /** @brief Get current configuration */
    const TinyBMS_Config& getConfig() const { return config_; }

    /** @brief Write configuration to TinyBMS */
    bool writeConfig(const TinyBMS_Config& config);

    /** @brief Bridge statistics (task-safe) */
    struct {
        uint32_t can_tx_count;
        uint32_t can_rx_count;
        uint32_t uart_errors;
        bool victron_keepalive_ok;
        uint32_t last_keepalive_ms;
        float cvl_current_v;
        CVL_State cvl_state;
    } stats;

private:
    // ============================= FreeRTOS Tasks =============================
    static void uartTask(void *pvParameters);
    static void canTask(void *pvParameters);
    static void cvlTask(void *pvParameters);

    // ============================ UART Communication ==========================
    HardwareSerial tiny_uart_{1};

    bool readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output);
    bool writeTinyRegisters(uint16_t start_addr, const uint16_t* values, uint8_t count);
    uint16_t calculateCRC(const uint8_t* data, size_t len);

    // ============================ CAN Communication ===========================
    bool sendVictronPGN(uint16_t pgn_id, const uint8_t* data, uint8_t dlc);
    void processVictronRX();

    // ============================= PGN Builders ===============================
    void buildPGN_0x351(uint8_t* data); // CVL/CCL/DCL
    void buildPGN_0x355(uint8_t* data); // SOC/SOH
    void buildPGN_0x356(uint8_t* data); // Voltage/Current/Temp
    void buildPGN_0x35A(uint8_t* data); // Alarms/Warnings
    void buildPGN_0x35E(uint8_t* data); // Manufacturer
    void buildPGN_0x35F(uint8_t* data); // Battery Info

    // ============================= CVL Algorithm ==============================
    void updateCVL();
    float calculateCVL();
    CVL_State determineCVLState();

    // ============================= Internal State =============================
    TinyBMS_LiveData live_data_;
    TinyBMS_Config config_;
    CVL_State cvl_state_;
    float cvl_voltage_;

    uint32_t last_cvl_update_ms_;
    uint32_t last_pgn_update_ms_;
    uint32_t last_uart_poll_ms_;
    uint32_t last_victron_keepalive_ms_;
    bool initialized_;
};

#endif // TINYBMS_VICTRON_BRIDGE_H