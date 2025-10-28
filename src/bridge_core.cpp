/**
 * @file bridge_core.cpp
 * @brief Core init & task creation
 */
#include <Arduino.h>
#include <algorithm>
#include "bridge_core.h"
#include "bridge_uart.h"
#include "bridge_can.h"
#include "bridge_cvl.h"
#include "bridge_keepalive.h"
#include "logger.h"
#include "event_bus.h"
#include "config_manager.h"
#include "watchdog_manager.h"
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "can_driver.h"

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;
extern SemaphoreHandle_t uartMutex;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[BRIDGE] ") + (msg)); } while(0)

TinyBMS_Victron_Bridge::TinyBMS_Victron_Bridge(IUartChannel& uart)
    : tiny_uart_(uart),
      initialized_(false),
      victron_keepalive_ok_(false) {
    memset(&live_data_, 0, sizeof(live_data_));
    memset(&config_, 0, sizeof(config_));
    stats = BridgeStats{};
    last_uart_poll_ms_ = last_pgn_update_ms_ = last_cvl_update_ms_ = 0;
    last_keepalive_tx_ms_ = last_keepalive_rx_ms_ = 0;
    uart_poll_interval_ms_ = UART_POLL_INTERVAL_MS;
    pgn_update_interval_ms_ = PGN_UPDATE_INTERVAL_MS;
    cvl_update_interval_ms_ = CVL_UPDATE_INTERVAL_MS;
    keepalive_interval_ms_ = 1000;
    keepalive_timeout_ms_ = 10000;
}

bool TinyBMS_Victron_Bridge::begin() {
    BRIDGE_LOG(LOG_INFO, "Initializing TinyBMS-Victron Bridge...");

    ConfigManager::HardwareConfig::UART uart_cfg{};
    ConfigManager::HardwareConfig::CAN can_cfg{};
    ConfigManager::TinyBMSConfig tinybms_cfg{};
    ConfigManager::VictronConfig victron_cfg{};

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_cfg = config.hardware.uart;
        can_cfg = config.hardware.can;
        tinybms_cfg = config.tinybms;
        victron_cfg = config.victron;
        xSemaphoreGive(configMutex);
    } else {
        BRIDGE_LOG(LOG_WARNING, "Using default configuration values (config mutex unavailable)");
    }

    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        tiny_uart_.begin(uart_cfg.baudrate, SERIAL_8N1,
                         uart_cfg.rx_pin, uart_cfg.tx_pin);
        xSemaphoreGive(uartMutex);
        BRIDGE_LOG(LOG_INFO, "UART initialized");
    } else {
        BRIDGE_LOG(LOG_ERROR, "UART mutex not available during init");
        return false;
    }

    BRIDGE_LOG(LOG_INFO, "Initializing CAN...");
    if (!CanDriver::begin(can_cfg.tx_pin,
                          can_cfg.rx_pin,
                          can_cfg.bitrate)) {
        BRIDGE_LOG(LOG_ERROR, "CAN init failed");
        return false;
    }
    BRIDGE_LOG(LOG_INFO, "CAN initialized OK");

    uart_poll_interval_ms_  = std::max<uint32_t>(20, tinybms_cfg.poll_interval_ms);
    pgn_update_interval_ms_ = std::max<uint32_t>(100, victron_cfg.pgn_update_interval_ms);
    cvl_update_interval_ms_ = std::max<uint32_t>(500, victron_cfg.cvl_update_interval_ms);
    keepalive_interval_ms_  = std::max<uint32_t>(200, victron_cfg.keepalive_interval_ms);
    keepalive_timeout_ms_   = std::max<uint32_t>(1000, victron_cfg.keepalive_timeout_ms);

    last_keepalive_rx_ms_ = millis();
    stats.victron_keepalive_ok = false;
    victron_keepalive_ok_ = false;

    BRIDGE_LOG(LOG_INFO, String("Intervals: UART=") + uart_poll_interval_ms_ +
                             "ms, PGN=" + pgn_update_interval_ms_ +
                             "ms, CVL=" + cvl_update_interval_ms_ +
                             "ms, KA tx=" + keepalive_interval_ms_ +
                             "ms, KA timeout=" + keepalive_timeout_ms_ + "ms");

    initialized_ = true;
    BRIDGE_LOG(LOG_INFO, "Bridge init complete");
    return true;
}

bool Bridge_CreateTasks(TinyBMS_Victron_Bridge* bridge) {
    if (!bridge || !bridge->initialized_) return false;

    const uint32_t uart_stack = TASK_DEFAULT_STACK_SIZE;
    const uint32_t can_stack  = TASK_DEFAULT_STACK_SIZE;
    const uint32_t cvl_stack  = TASK_DEFAULT_STACK_SIZE;

    BaseType_t ok1 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::uartTask, "UART_Task",
                      uart_stack, bridge, TASK_HIGH_PRIORITY, nullptr, 1);
    BaseType_t ok2 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::canTask, "CAN_Task",
                      can_stack, bridge, TASK_HIGH_PRIORITY, nullptr, 1);
    BaseType_t ok3 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::cvlTask, "CVL_Task",
                      cvl_stack, bridge, TASK_NORMAL_PRIORITY, nullptr, 1);

    return (ok1 == pdPASS && ok2 == pdPASS && ok3 == pdPASS);
}

TinyBMS_LiveData TinyBMS_Victron_Bridge::getLiveData() const {
    return live_data_;
}

TinyBMS_Config TinyBMS_Victron_Bridge::getConfig() const {
    return config_;
}
