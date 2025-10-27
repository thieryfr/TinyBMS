
/**
 * @file bridge_core.cpp
 * @brief Core init & task creation
 */
#include <Arduino.h>
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
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, "[BRIDGE] " msg); } while(0)

TinyBMS_Victron_Bridge::TinyBMS_Victron_Bridge()
: last_uart_poll_ms_(0), last_pgn_update_ms_(0), last_cvl_update_ms_(0),
  initialized_(false), victron_keepalive_ok_(false),
  last_keepalive_tx_ms_(0), last_keepalive_rx_ms_(0) {
    memset(&live_data_, 0, sizeof(live_data_));
    memset(&stats, 0, sizeof(stats));
}

bool TinyBMS_Victron_Bridge::begin() {
    BRIDGE_LOG(LOG_INFO, "Initializing TinyBMS-Victron Bridge...");

    // UART init
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        tiny_uart_.begin(config.hardware.uart.baudrate, SERIAL_8N1,
                         config.hardware.uart.rx_pin, config.hardware.uart.tx_pin);
        xSemaphoreGive(uartMutex);
        BRIDGE_LOG(LOG_INFO, "UART initialized");
    } else {
        BRIDGE_LOG(LOG_ERROR, "UART mutex not available during init");
        return false;
    }

    // CAN init
    BRIDGE_LOG(LOG_INFO, "Initializing CAN...");
    if (!CanDriver::begin(config.hardware.can.tx_pin,
                          config.hardware.can.rx_pin,
                          config.hardware.can.bitrate)) {
        BRIDGE_LOG(LOG_ERROR, "CAN init failed");
        return false;
    }
    BRIDGE_LOG(LOG_INFO, "CAN initialized OK");

    initialized_ = true;
    BRIDGE_LOG(LOG_INFO, "Bridge init complete");
    return true;
}

bool Bridge_CreateTasks(TinyBMS_Victron_Bridge* bridge) {
    if (!bridge || !bridge->initialized_) return false;

    BaseType_t ok1 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::uartTask, "UART_Task",
                      UART_TASK_STACK, bridge, UART_TASK_PRIO, nullptr, 1);
    BaseType_t ok2 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::canTask, "CAN_Task",
                      CAN_TASK_STACK, bridge, CAN_TASK_PRIO, nullptr, 1);
    BaseType_t ok3 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::cvlTask, "CVL_Task",
                      CVL_TASK_STACK, bridge, CVL_TASK_PRIO, nullptr, 1);

    return (ok1 == pdPASS && ok2 == pdPASS && ok3 == pdPASS);
}
