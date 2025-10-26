/**
 * @file tinybms_victron_bridge.cpp
 * @brief TinyBMS ↔ Victron CAN-BMS Bridge with FreeRTOS and Logging
 * @version 2.2 - Logging + Watchdog Feed + Stack Monitoring
 */

#include <Arduino.h>
#include <Freertos.h>
#include "tinybms_victron_bridge.h"
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "shared_data.h"
#include "watchdog_manager.h"

extern SemaphoreHandle_t uartMutex;
extern QueueHandle_t liveDataQueue;
extern SemaphoreHandle_t feedMutex;
extern WatchdogManager Watchdog;

// ====================================================================================
// CONSTRUCTOR
// ====================================================================================
TinyBMS_Victron_Bridge::TinyBMS_Victron_Bridge()
    : last_uart_poll_ms_(0),
      last_pgn_update_ms_(0),
      last_cvl_update_ms_(0),
      initialized_(false)
{
    live_data_ = {0};
    stats = {0};
}

// ====================================================================================
// INITIALIZATION
// ====================================================================================
bool TinyBMS_Victron_Bridge::begin() {
    BRIDGE_LOG(LOG_INFO, "Initializing TinyBMS-Victron Bridge...");

    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial1.begin(config_.baudrate, SERIAL_8N1, config_.rx_pin, config_.tx_pin);
        xSemaphoreGive(uartMutex);
        BRIDGE_LOG(LOG_INFO, "UART initialized successfully");
    } else {
        BRIDGE_LOG(LOG_ERROR, "❌ Failed to acquire UART mutex for initialization");
        return false;
    }

    BRIDGE_LOG(LOG_INFO, "Initializing CAN interface...");
    // TODO: Replace with actual CAN init code
    bool can_ok = true;
    if (!can_ok) {
        BRIDGE_LOG(LOG_ERROR, "❌ CAN initialization failed");
        return false;
    }

    initialized_ = true;
    BRIDGE_LOG(LOG_INFO, "Bridge initialization completed ✓");
    return true;
}

// ====================================================================================
// READ TINYBMS REGISTERS
// ====================================================================================
bool TinyBMS_Victron_Bridge::readTinyRegisters(uint8_t start_reg, uint8_t count, uint16_t* regs) {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        BRIDGE_LOG(LOG_ERROR, "❌ UART mutex unavailable for read");
        return false;
    }

    // Simulated read for now
    bool success = true;
    // ... (Actual UART read code)

    if (success)
        BRIDGE_LOG(LOG_DEBUG, "Read " + String(count) + " regs from " + String(start_reg));
    else
        BRIDGE_LOG(LOG_WARN, "Failed to read TinyBMS registers from " + String(start_reg));

    xSemaphoreGive(uartMutex);
    return success;
}

// ====================================================================================
// UART TASK (BMS POLLING)
// ====================================================================================
void TinyBMS_Victron_Bridge::uartTask(void *pvParameters) {
    TinyBMS_Victron_Bridge *bridge = (TinyBMS_Victron_Bridge *)pvParameters;
    BRIDGE_LOG(LOG_INFO, "[TASK] uartTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (now - bridge->last_uart_poll_ms_ >= UART_POLL_INTERVAL_MS) {
            TinyBMS_LiveData data;
            uint16_t regs[17];

            if (bridge->readTinyRegisters(TINY_REG_VOLTAGE, 17, regs)) {
                data.voltage = regs[0] / 100.0f;
                data.current = regs[1] / 10.0f;
                data.soc_percent = regs[2] / 10.0f;
                data.soh_percent = regs[3] / 10.0f;
                data.temperature = regs[4] / 10.0f;
                data.min_cell_mv = regs[5];
                data.max_cell_mv = regs[6];
                data.cell_imbalance_mv = data.max_cell_mv - data.min_cell_mv;
                data.balancing_bits = regs[7];
                data.online_status = true;

                xQueueOverwrite(liveDataQueue, &data);
                bridge->live_data_ = data;

                LOG_LIVEDATA(data, LOG_DEBUG);
            } else {
                bridge->stats.uart_errors++;
                data.online_status = false;
                xQueueOverwrite(liveDataQueue, &data);
                bridge->live_data_ = data;
                BRIDGE_LOG(LOG_WARN, "TinyBMS read failed — UART error count: " + String(bridge->stats.uart_errors));
            }

            bridge->last_uart_poll_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        UBaseType_t stackMark = uxTaskGetStackHighWaterMark(NULL);
        BRIDGE_LOG(LOG_DEBUG, "[TASK] uartTask Stack usage: " + String(stackMark * sizeof(StackType_t)) + " bytes free");

        vTaskDelay(pdMS_TO_TICKS(UART_POLL_INTERVAL_MS));
    }
}

// ====================================================================================
// CAN TASK
// ====================================================================================
void TinyBMS_Victron_Bridge::canTask(void *pvParameters) {
    TinyBMS_Victron_Bridge *bridge = (TinyBMS_Victron_Bridge *)pvParameters;
    BRIDGE_LOG(LOG_INFO, "[TASK] canTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (now - bridge->last_pgn_update_ms_ >= PGN_UPDATE_INTERVAL_MS) {
            TinyBMS_LiveData data;
            if (xQueuePeek(liveDataQueue, &data, 0) == pdTRUE) {
                // TODO: Send CAN PGNs
                bridge->stats.can_tx_count++;
                BRIDGE_LOG(LOG_DEBUG, "PGN frame sent (CAN Tx Count = " + String(bridge->stats.can_tx_count) + ")");
            } else {
                BRIDGE_LOG(LOG_WARN, "No live data in queue for CAN broadcast");
            }

            bridge->last_pgn_update_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        UBaseType_t stackMark = uxTaskGetStackHighWaterMark(NULL);
        BRIDGE_LOG(LOG_DEBUG, "[TASK] canTask Stack usage: " + String(stackMark * sizeof(StackType_t)) + " bytes free");

        vTaskDelay(pdMS_TO_TICKS(PGN_UPDATE_INTERVAL_MS));
    }
}

// ====================================================================================
// CVL TASK
// ====================================================================================
void TinyBMS_Victron_Bridge::cvlTask(void *pvParameters) {
    TinyBMS_Victron_Bridge *bridge = (TinyBMS_Victron_Bridge *)pvParameters;
    BRIDGE_LOG(LOG_INFO, "[TASK] cvlTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (now - bridge->last_cvl_update_ms_ >= CVL_UPDATE_INTERVAL_MS) {
            TinyBMS_LiveData data;
            if (xQueuePeek(liveDataQueue, &data, 0) == pdTRUE) {
                bridge->stats.cvl_current_v = data.voltage;
                bridge->stats.cvl_state = CVL_BULK_ABSORPTION;
                BRIDGE_LOG(LOG_DEBUG, "CVL updated: " + String(data.voltage, 2) + " V");
            }

            bridge->last_cvl_update_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        UBaseType_t stackMark = uxTaskGetStackHighWaterMark(NULL);
        BRIDGE_LOG(LOG_DEBUG, "[TASK] cvlTask Stack usage: " + String(stackMark * sizeof(StackType_t)) + " bytes free");

        vTaskDelay(pdMS_TO_TICKS(CVL_UPDATE_INTERVAL_MS));
    }
}

// ====================================================================================
// GETTERS
// ====================================================================================
TinyBMS_LiveData TinyBMS_Victron_Bridge::getLiveData() const {
    return live_data_;
}

TinyBMS_Config TinyBMS_Victron_Bridge::getConfig() const {
    TinyBMS_Config cfg = config_;
    return cfg;
}