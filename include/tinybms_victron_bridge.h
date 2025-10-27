
/**
 * @file tinybms_victron_bridge.h
 * @brief Facade header for TinyBMS ↔ Victron bridge (split by tasks/modules)
 * @version 2.4.0
 */
#pragma once
#include <Arduino.h>
#include "shared_data.h"

// Forward decls for external singletons/resources (provided elsewhere in project)
class HardwareSerial;
class WatchdogManager;
class ConfigManager;
class Logger;
class EventBus;

enum CVLState : uint8_t {
    CVL_IDLE = 0,
    CVL_BULK_ABSORPTION = 1,
    CVL_FLOAT = 2
};

struct BridgeStats {
    uint32_t can_tx_count = 0;
    uint32_t can_tx_errors = 0;
    uint32_t uart_errors = 0;
    float    cvl_current_v = 0.0f;
    CVLState cvl_state = CVL_IDLE;
};

class TinyBMS_Victron_Bridge {
public:
    TinyBMS_Victron_Bridge();

    // ---- Core init
    bool begin();

    // ---- Tasks
    static void uartTask(void *pvParameters);
    static void canTask(void *pvParameters);
    static void cvlTask(void *pvParameters);

    // ---- UART helpers
    bool readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output);

    // ---- CAN helpers
    bool sendVictronPGN(uint16_t pgn_id, const uint8_t* data, uint8_t dlc);

    // ---- PGN builders
    void buildPGN_0x351(uint8_t* d); // CVL / CCL / DCL
    void buildPGN_0x355(uint8_t* d); // SOC / SOH
    void buildPGN_0x356(uint8_t* d); // U / I / T
    void buildPGN_0x35A(uint8_t* d); // Alarms / Warnings
    void buildPGN_0x35E(uint8_t* d); // Manufacturer (8 ASCII)
    void buildPGN_0x35F(uint8_t* d); // Battery name/info (8 ASCII)

    // ---- KeepAlive (0x305)
    void keepAliveSend();                   // TX 0x305 (1 Hz)
    void keepAliveProcessRX(uint32_t now);  // RX polling + timeout management

    // ---- Getters
    TinyBMS_LiveData getLiveData() const;
    TinyBMS_Config   getConfig() const;

public:
    // public because tasks are C-style
    HardwareSerial& tiny_uart_ = Serial1;

    // live data snapshot used by CAN task
    TinyBMS_LiveData live_data_{};
    TinyBMS_Config   config_{};
    BridgeStats      stats{};

    // runtime flags
    bool initialized_ = false;
    bool victron_keepalive_ok_ = false;

    // timers (ms)
    uint32_t last_uart_poll_ms_   = 0;
    uint32_t last_pgn_update_ms_  = 0;
    uint32_t last_cvl_update_ms_  = 0;
    uint32_t last_keepalive_tx_ms_= 0;
    uint32_t last_keepalive_rx_ms_= 0;
};

