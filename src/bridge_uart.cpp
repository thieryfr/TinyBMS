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

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;
extern SemaphoreHandle_t uartMutex;
extern SemaphoreHandle_t feedMutex;
extern WatchdogManager Watchdog;
extern SemaphoreHandle_t configMutex;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[UART] ") + (msg)); } while(0)

namespace {

constexpr uint8_t TINYBMS_SLAVE_ADDRESS = 0x01;
constexpr uint8_t MODBUS_READ_HOLDING_REGS = 0x03;

uint16_t modbusCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

} // namespace

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

    std::fill_n(output, count, static_cast<uint16_t>(0));

    uint8_t attempt_count = 3;
    uint32_t retry_delay_ms = 50;
    uint32_t response_timeout_ms = 100;

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        attempt_count = std::max<uint8_t>(static_cast<uint8_t>(1), config.tinybms.uart_retry_count);
        retry_delay_ms = config.tinybms.uart_retry_delay_ms;
        response_timeout_ms = std::max<uint32_t>(20, static_cast<uint32_t>(config.hardware.uart.timeout_ms));
        xSemaphoreGive(configMutex);
    } else {
        BRIDGE_LOG(LOG_WARN, "Using default UART retry configuration (config mutex unavailable)");
    }

    const size_t expected_data_bytes = static_cast<size_t>(count) * 2U;
    const size_t expected_response_len = 3 + expected_data_bytes + 2;
    if (expected_response_len > 256) {
        BRIDGE_LOG(LOG_ERROR, "Requested register span too large");
        xSemaphoreGive(uartMutex);
        return false;
    }

    uint32_t retries_used = 0;
    bool success = false;
    uint32_t previous_timeout = tiny_uart_.getTimeout();
    tiny_uart_.setTimeout(response_timeout_ms);

    for (uint8_t attempt = 0; attempt < attempt_count && !success; ++attempt) {
        if (attempt > 0) {
            ++retries_used;
            if (retry_delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            }
        }

        while (tiny_uart_.available() > 0) {
            tiny_uart_.read();
        }

        uint8_t request[8];
        request[0] = TINYBMS_SLAVE_ADDRESS;
        request[1] = MODBUS_READ_HOLDING_REGS;
        request[2] = static_cast<uint8_t>((start_addr >> 8) & 0xFF);
        request[3] = static_cast<uint8_t>(start_addr & 0xFF);
        request[4] = static_cast<uint8_t>((count >> 8) & 0xFF);
        request[5] = static_cast<uint8_t>(count & 0xFF);
        const uint16_t crc = modbusCRC16(request, 6);
        request[6] = static_cast<uint8_t>(crc & 0xFF);
        request[7] = static_cast<uint8_t>((crc >> 8) & 0xFF);

        size_t written = tiny_uart_.write(request, sizeof(request));
        tiny_uart_.flush();
        if (written != sizeof(request)) {
            BRIDGE_LOG(LOG_ERROR, "Failed to write full Modbus request");
            continue;
        }

        uint8_t response[256];
        size_t received = tiny_uart_.readBytes(response, expected_response_len);
        if (received != expected_response_len) {
            BRIDGE_LOG(LOG_WARN, String("UART timeout (received ") + received + " / " + expected_response_len + " bytes)");
            stats.uart_timeouts++;
            continue;
        }

        const uint16_t resp_crc = static_cast<uint16_t>(response[received - 2]) |
                                   (static_cast<uint16_t>(response[received - 1]) << 8);
        const uint16_t calc_crc = modbusCRC16(response, received - 2);
        if (resp_crc != calc_crc) {
            BRIDGE_LOG(LOG_WARN, "CRC mismatch on TinyBMS response");
            stats.uart_crc_errors++;
            continue;
        }

        if (response[1] & 0x80) {
            BRIDGE_LOG(LOG_ERROR, String("TinyBMS Modbus exception: 0x") + String(response[2], HEX));
            continue;
        }

        if (response[0] != TINYBMS_SLAVE_ADDRESS || response[1] != MODBUS_READ_HOLDING_REGS) {
            BRIDGE_LOG(LOG_ERROR, "Unexpected Modbus header in TinyBMS response");
            continue;
        }

        if (response[2] != expected_data_bytes) {
            BRIDGE_LOG(LOG_ERROR, "Unexpected Modbus payload length");
            continue;
        }

        for (uint16_t i = 0; i < count; ++i) {
            const size_t idx = 3 + i * 2;
            output[i] = static_cast<uint16_t>(response[idx] << 8 | response[idx + 1]);
        }

        success = true;
        stats.uart_success_count++;
    }

    tiny_uart_.setTimeout(previous_timeout);
    stats.uart_retry_count += retries_used;

    if (!success) {
        stats.uart_errors++;
    }

    xSemaphoreGive(uartMutex);
    return success;
}

void TinyBMS_Victron_Bridge::uartTask(void *pvParameters) {
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "uartTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - bridge->last_uart_poll_ms_ >= bridge->uart_poll_interval_ms_) {
            TinyBMS_LiveData d{};
            uint16_t regs[24];

            if (bridge->readTinyRegisters(TINY_REG_VOLTAGE, 17, regs)) {
                d.voltage            = regs[0] / 100.0f;
                d.current            = ((int16_t)regs[1]) / 10.0f;
                d.soc_percent        = regs[2] / 10.0f;
                d.soh_percent        = regs[3] / 10.0f;
                d.temperature        = regs[4];
                d.min_cell_mv        = regs[5];
                d.max_cell_mv        = regs[6];
                d.cell_imbalance_mv  = d.max_cell_mv - d.min_cell_mv;
                d.balancing_bits     = regs[7];
                d.max_charge_current = regs[8];
                d.max_discharge_current = regs[9];
                d.online_status      = 0x91;

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
