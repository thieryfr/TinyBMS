/**
 * @file tinybms_victron_bridge.h
 * @brief Facade header for TinyBMS ↔ Victron bridge (split by tasks/modules)
 * @version 2.5.0
 */
#pragma once

#include <Arduino.h>
#include "shared_data.h"
#include "cvl_types.h"
#include "uart/uart_channel.h"

class HardwareSerial;
class WatchdogManager;
class ConfigManager;
class Logger;
class EventBus;

struct TinyBMS_Config {
    uint16_t fully_charged_voltage_mv = 0;
    uint16_t fully_discharged_voltage_mv = 0;
    uint16_t charge_finished_current_ma = 0;
    float    battery_capacity_ah = 0.0f;
    uint8_t  cell_count = 0;
    uint16_t overvoltage_cutoff_mv = 0;
    uint16_t undervoltage_cutoff_mv = 0;
    uint16_t discharge_overcurrent_a = 0;
    uint16_t charge_overcurrent_a = 0;
    float    overheat_cutoff_c = 0.0f;
    float    low_temp_charge_cutoff_c = 0.0f;
};

struct BridgeStats {
    uint32_t can_tx_count = 0;
    uint32_t can_rx_count = 0;
    uint32_t can_tx_errors = 0;
    uint32_t can_rx_errors = 0;
    uint32_t can_bus_off_count = 0;
    uint32_t can_queue_overflows = 0;
    uint32_t uart_errors = 0;
    uint32_t uart_success_count = 0;
    uint32_t uart_timeouts = 0;
    uint32_t uart_crc_errors = 0;
    uint32_t uart_retry_count = 0;
    float    cvl_current_v = 0.0f;
    float    ccl_limit_a = 0.0f;
    float    dcl_limit_a = 0.0f;
    double   energy_charged_wh = 0.0;
    double   energy_discharged_wh = 0.0;
    CVLState cvl_state = CVL_BULK;
    bool     victron_keepalive_ok = false;
};

namespace mqtt {
class Publisher;
} // namespace mqtt

class TinyBMS_Victron_Bridge {
public:
    explicit TinyBMS_Victron_Bridge(IUartChannel& uart = defaultTinyBmsUart());

    bool begin();

    void setMqttPublisher(mqtt::Publisher* publisher);

    static void uartTask(void *pvParameters);
    static void canTask(void *pvParameters);
    static void cvlTask(void *pvParameters);

    bool readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output);

    bool sendVictronPGN(uint16_t pgn_id, const uint8_t* data, uint8_t dlc);

    void buildPGN_0x351(uint8_t* d);
    void buildPGN_0x355(uint8_t* d);
    void buildPGN_0x356(uint8_t* d);
    void buildPGN_0x35A(uint8_t* d);
    void buildPGN_0x35E(uint8_t* d);
    void buildPGN_0x35F(uint8_t* d);
    void buildPGN_0x371(uint8_t* d);
    void buildPGN_0x378(uint8_t* d);
    void buildPGN_0x379(uint8_t* d);
    void buildPGN_0x382(uint8_t* d);

    void keepAliveSend();
    void keepAliveProcessRX(uint32_t now);

    TinyBMS_LiveData getLiveData() const;
    TinyBMS_Config   getConfig() const;

public:
    IUartChannel& tiny_uart_;

    TinyBMS_LiveData live_data_{};
    TinyBMS_Config   config_{};
    BridgeStats      stats{};

    mqtt::Publisher* mqtt_publisher_ = nullptr;

    bool initialized_ = false;
    bool victron_keepalive_ok_ = false;

    uint32_t last_uart_poll_ms_   = 0;
    uint32_t last_pgn_update_ms_  = 0;
    uint32_t last_cvl_update_ms_  = 0;
    uint32_t last_keepalive_tx_ms_= 0;
    uint32_t last_keepalive_rx_ms_= 0;

    uint32_t uart_poll_interval_ms_  = 100;
    uint32_t pgn_update_interval_ms_ = 1000;
    uint32_t cvl_update_interval_ms_ = 20000;
    uint32_t keepalive_interval_ms_  = 1000;
    uint32_t keepalive_timeout_ms_   = 10000;

private:
    void updateEnergyCounters(uint32_t now_ms);

    uint32_t last_energy_update_ms_ = 0;
};
