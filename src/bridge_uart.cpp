
/**
 * @file bridge_uart.cpp
 * @brief UART polling from TinyBMS → EventBus
 */
#include <Arduino.h>
#include "bridge_uart.h"
#include "logger.h"
#include "event_bus.h"
#include "config_manager.h"
#include "watchdog_manager.h"
#include "rtos_config.h"

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;
extern SemaphoreHandle_t uartMutex;
extern SemaphoreHandle_t feedMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[UART] ") + msg); } while(0)

bool TinyBMS_Victron_Bridge::readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output) {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        BRIDGE_LOG(LOG_ERROR, "UART mutex unavailable for read");
        return false;
    }
    // TODO: Replace with real TinyBMS read (MODBUS/proprietary) using start/count
    for (uint16_t i = 0; i < count; ++i) output[i] = 0;
    xSemaphoreGive(uartMutex);
    return true;
}

void TinyBMS_Victron_Bridge::uartTask(void *pvParameters) {
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "uartTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - bridge->last_uart_poll_ms_ >= UART_POLL_INTERVAL_MS) {
            TinyBMS_LiveData d{};
            uint16_t regs[24];

            if (bridge->readTinyRegisters(TINY_REG_VOLTAGE, 17, regs)) {
                d.voltage            = regs[0] / 100.0f;           // 0.01V
                d.current            = ((int16_t)regs[1]) / 10.0f; // 0.1A signed
                d.soc_percent        = regs[2] / 10.0f;            // 0.1%
                d.soh_percent        = regs[3] / 10.0f;            // 0.1%
                d.temperature        = regs[4];                    // 0.1°C raw
                d.min_cell_mv        = regs[5];
                d.max_cell_mv        = regs[6];
                d.cell_imbalance_mv  = d.max_cell_mv - d.min_cell_mv;
                d.balancing_bits     = regs[7];
                d.max_charge_current = regs[8];                    // 0.1A
                d.max_discharge_current = regs[9];                 // 0.1A
                d.online_status      = 0x91;                       // example state

                bridge->live_data_ = d;
                eventBus.publishLiveData(d, SOURCE_ID_UART);

                // Example alarms
                const float V = d.voltage;
                const float T = d.temperature / 10.0f;
                if (V > 58.4f) eventBus.publishAlarm(ALARM_OVERVOLTAGE, "Voltage high", ALARM_SEVERITY_ERROR, V, SOURCE_ID_UART);
                if (V > 0.1f && V < 44.0f) eventBus.publishAlarm(ALARM_UNDERVOLTAGE, "Voltage low", ALARM_SEVERITY_WARNING, V, SOURCE_ID_UART);
                if (d.cell_imbalance_mv > 200) eventBus.publishAlarm(ALARM_CELL_IMBALANCE, "Imbalance > 200mV", ALARM_SEVERITY_WARNING, d.cell_imbalance_mv, SOURCE_ID_UART);
                if (T > 55.0f) eventBus.publishAlarm(ALARM_OVERTEMPERATURE, "Temp high", ALARM_SEVERITY_ERROR, T, SOURCE_ID_UART);
                if (T < 0.0f && d.current > 3.0f) eventBus.publishAlarm(ALARM_LOW_T_CHARGE, "Charging at low T", ALARM_SEVERITY_WARNING, T, SOURCE_ID_UART);
            } else {
                bridge->stats.uart_errors++;
                eventBus.publishAlarm(ALARM_UART_ERROR, "TinyBMS UART error", ALARM_SEVERITY_WARNING, bridge->stats.uart_errors, SOURCE_ID_UART);
            }

            bridge->last_uart_poll_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(UART_POLL_INTERVAL_MS));
    }
}
