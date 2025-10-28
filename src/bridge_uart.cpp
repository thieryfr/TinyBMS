/**
 * @file bridge_uart.cpp
 * @brief UART polling from TinyBMS → EventBus
 */
#include <Arduino.h>
#include <algorithm>
#include "bridge_uart.h"
#include "logger.h"
#include "event_bus.h"
#include "config_manager.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "uart/tinybms_uart_client.h"
#include "tiny_read_mapping.h"

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;
extern SemaphoreHandle_t uartMutex;
extern SemaphoreHandle_t feedMutex;
extern WatchdogManager Watchdog;
extern SemaphoreHandle_t configMutex;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[UART] ") + (msg)); } while(0)

namespace {
constexpr uint16_t TINY_REG_VOLTAGE = 36;
constexpr uint16_t TINY_READ_COUNT = 17;
}

bool TinyBMS_Victron_Bridge::readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output) {
    if (output == nullptr || count == 0) {
        BRIDGE_LOG(LOG_ERROR, "Invalid readTinyRegisters arguments");
        return false;
    }

    const TickType_t mutex_timeout = pdMS_TO_TICKS(100);
    if (xSemaphoreTake(uartMutex, mutex_timeout) != pdTRUE) {
        BRIDGE_LOG(LOG_ERROR, "UART mutex unavailable for read");
        stats.uart_errors++;
        stats.uart_timeouts++;
        return false;
    }

    tinybms::TransactionOptions options{};
    options.attempt_count = 3;
    options.retry_delay_ms = 50;
    options.response_timeout_ms = 100;

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        options.attempt_count = std::max<uint8_t>(static_cast<uint8_t>(1), config.tinybms.uart_retry_count);
        options.retry_delay_ms = config.tinybms.uart_retry_delay_ms;
        options.response_timeout_ms = std::max<uint32_t>(20, static_cast<uint32_t>(config.hardware.uart.timeout_ms));
        xSemaphoreGive(configMutex);
    } else {
        BRIDGE_LOG(LOG_WARN, "Using default UART retry configuration (config mutex unavailable)");
    }

    auto delay_adapter = [](uint32_t delay_ms, void*) {
        if (delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    };

    tinybms::DelayConfig delay_config{delay_adapter, nullptr};
    tinybms::TransactionResult result = tinybms::readHoldingRegisters(
        tiny_uart_, start_addr, count, output, options, delay_config);

    stats.uart_retry_count += result.retries_performed;
    stats.uart_timeouts += result.timeout_count;
    stats.uart_crc_errors += result.crc_error_count;
    stats.uart_success_count += result.success ? 1 : 0;
    if (!result.success) {
        stats.uart_errors++;
    }

    switch (result.last_status) {
        case tinybms::AttemptStatus::Timeout:
            BRIDGE_LOG(LOG_WARN, String("UART timeout after ") + options.attempt_count + " attempt(s)");
            break;
        case tinybms::AttemptStatus::CrcMismatch:
            BRIDGE_LOG(LOG_WARN, "CRC mismatch on TinyBMS response");
            break;
        case tinybms::AttemptStatus::WriteError:
            BRIDGE_LOG(LOG_ERROR, "Failed to write full Modbus request");
            break;
        case tinybms::AttemptStatus::ProtocolError:
            if (!result.success) {
                BRIDGE_LOG(LOG_ERROR, "TinyBMS protocol error during Modbus read");
            }
            break;
        case tinybms::AttemptStatus::Success:
            break;
    }

    xSemaphoreGive(uartMutex);
    return result.success;
}

void TinyBMS_Victron_Bridge::uartTask(void *pvParameters) {
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "uartTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - bridge->last_uart_poll_ms_ >= bridge->uart_poll_interval_ms_) {
            TinyBMS_LiveData d{};
            d.resetSnapshots();
            uint16_t regs[TINY_READ_COUNT] = {0};

            if (bridge->readTinyRegisters(TINY_REG_VOLTAGE, TINY_READ_COUNT, regs)) {
                const auto& bindings = getTinyRegisterBindings();
                for (const auto& binding : bindings) {
                    if (binding.raw_index >= TINY_READ_COUNT) {
                        continue;
                    }

                    int32_t raw_value = binding.is_signed
                        ? static_cast<int16_t>(regs[binding.raw_index])
                        : static_cast<int32_t>(regs[binding.raw_index]);
                    float scaled_value = static_cast<float>(raw_value) * binding.scale;
                    d.applyBinding(binding, raw_value, scaled_value);
                }

                d.cell_imbalance_mv = (d.max_cell_mv > d.min_cell_mv)
                    ? static_cast<uint16_t>(d.max_cell_mv - d.min_cell_mv)
                    : 0;

                d.online_status = 0x91;

                bridge->live_data_ = d;
                eventBus.publishLiveData(d, SOURCE_ID_UART);

                const auto& th = config.victron.thresholds;
                const float V = d.voltage;
                const float T = d.temperature / 10.0f;

                if (V > th.overvoltage_v) {
                    eventBus.publishAlarm(ALARM_OVERVOLTAGE, "Voltage high", ALARM_SEVERITY_ERROR, V, SOURCE_ID_UART);
                }
                if (V > 0.1f && V < th.undervoltage_v) {
                    eventBus.publishAlarm(ALARM_UNDERVOLTAGE, "Voltage low", ALARM_SEVERITY_WARNING, V, SOURCE_ID_UART);
                }
                if (d.cell_imbalance_mv > th.imbalance_alarm_mv) {
                    eventBus.publishAlarm(ALARM_CELL_IMBALANCE, "Imbalance above alarm threshold", ALARM_SEVERITY_WARNING, d.cell_imbalance_mv, SOURCE_ID_UART);
                }
                if (T > th.overtemp_c) {
                    eventBus.publishAlarm(ALARM_OVERTEMPERATURE, "Temp high", ALARM_SEVERITY_ERROR, T, SOURCE_ID_UART);
                }
                if (T < th.low_temp_charge_c && d.current > 3.0f) {
                    eventBus.publishAlarm(ALARM_LOW_T_CHARGE, "Charging at low T", ALARM_SEVERITY_WARNING, T, SOURCE_ID_UART);
                }
            } else {
                eventBus.publishAlarm(ALARM_UART_ERROR, "TinyBMS UART error", ALARM_SEVERITY_WARNING, bridge->stats.uart_errors, SOURCE_ID_UART);
            }

            bridge->last_uart_poll_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(bridge->uart_poll_interval_ms_));
    }
}
