/**
 * @file bridge_uart.cpp
 * @brief UART polling from TinyBMS → EventBus
 */
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <map>
#include <vector>
#include <cstring>
#include "bridge_uart.h"
#include "logger.h"
#include "config_manager.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "uart/tinybms_uart_client.h"
#include "tiny_read_mapping.h"
#include "mqtt/publisher.h"
#include "event/event_types_v2.h"
#include "hal/hal_config.h"
#include "hal/interfaces/ihal_uart.h"

using tinybms::events::AlarmCode;
using tinybms::events::AlarmRaised;
using tinybms::events::AlarmSeverity;
using tinybms::events::EventSource;
using tinybms::events::LiveDataUpdate;
using tinybms::events::MqttRegisterValue;
using tinybms::events::MqttRegisterEvent;
using tinybms::events::WarningRaised;

extern Logger logger;
extern ConfigManager config;
extern SemaphoreHandle_t uartMutex;
extern SemaphoreHandle_t feedMutex;
extern WatchdogManager Watchdog;
extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t statsMutex;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[UART] ") + (msg)); } while(0)

namespace {
class RingBufferedHalUart : public hal::IHalUart {
public:
    RingBufferedHalUart(hal::IHalUart& upstream, optimization::ByteRingBuffer& buffer)
        : upstream_(upstream), buffer_(buffer) {}

    hal::Status initialize(const hal::UartConfig& config) override {
        return upstream_.initialize(config);
    }

    void setTimeout(uint32_t timeout_ms) override {
        upstream_.setTimeout(timeout_ms);
    }

    uint32_t getTimeout() const override {
        return upstream_.getTimeout();
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        return upstream_.write(buffer, size);
    }

    void flush() override {
        upstream_.flush();
    }

    size_t readBytes(uint8_t* buffer, size_t length) override {
        size_t read = upstream_.readBytes(buffer, length);
        if (read > 0) {
            buffer_.push(buffer, read);
        }
        return read;
    }

    int available() override {
        return upstream_.available();
    }

    int read() override {
        int value = upstream_.read();
        if (value >= 0) {
            uint8_t byte = static_cast<uint8_t>(value & 0xFF);
            buffer_.push(&byte, 1);
        }
        return value;
    }

private:
    hal::IHalUart& upstream_;
    optimization::ByteRingBuffer& buffer_;
};

struct TinyRegisterReadBlock {
    uint16_t start;
    uint16_t count;
};

constexpr TinyRegisterReadBlock kTinyReadBlocks[] = {
    {32, 21},   // Lifetime counter + primary live data window
    {102, 2},   // Charge/discharge limits
    {113, 2},   // Pack temperature min/max
    {305, 3},   // Victron keep-alive and handshake window
    {315, 5},   // Protection thresholds (voltage/current/temp)
    {500, 6}    // Manufacturer / firmware / family strings
};

constexpr size_t kTinyReadBlockCount = sizeof(kTinyReadBlocks) / sizeof(kTinyReadBlocks[0]);
constexpr uint16_t kTinyMaxReadWords = 32;

void publishAlarmEvent(BridgeEventSink& sink,
                       EventSource source,
                       AlarmCode code,
                       const char* message,
                       AlarmSeverity severity,
                       float value) {
    AlarmRaised event{};
    event.metadata.source = source;
    event.alarm.alarm_code = static_cast<uint16_t>(code);
    event.alarm.severity = static_cast<uint8_t>(severity);
    if (message) {
        std::strncpy(event.alarm.message, message, sizeof(event.alarm.message) - 1);
        event.alarm.message[sizeof(event.alarm.message) - 1] = '\0';
    }
    event.alarm.value = value;
    event.alarm.is_active = true;
    sink.publish(event);
}
}

bool TinyBMS_Victron_Bridge::readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output) {
    if (output == nullptr || count == 0) {
        BRIDGE_LOG(LOG_ERROR, "Invalid readTinyRegisters arguments");
        return false;
    }

    if (tiny_uart_ == nullptr) {
        BRIDGE_LOG(LOG_ERROR, "UART HAL not available");
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

    // Phase 2: Increase configMutex timeout from 25ms to 100ms (consistency)
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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
    const uint32_t start_ms = millis();
    uart_rx_buffer_.clear();
    RingBufferedHalUart buffered_uart(*tiny_uart_, uart_rx_buffer_);
    tinybms::TransactionResult result = tinybms::readHoldingRegisters(
        buffered_uart, start_addr, count, output, options, delay_config);

    const uint32_t elapsed_ms = millis() - start_ms;

    if (result.success) {
        uart_poller_.recordSuccess(elapsed_ms, static_cast<uint32_t>(count) * 2U);
    } else {
        if (result.last_status == tinybms::AttemptStatus::Timeout) {
            uart_poller_.recordTimeout();
        } else {
            uart_poller_.recordFailure(elapsed_ms);
        }
    }

    uart_poll_interval_ms_ = uart_poller_.currentInterval();

    bool stats_updated = false;
    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.uart_retry_count += result.retries_performed;
        stats.uart_timeouts += result.timeout_count;
        stats.uart_crc_errors += result.crc_error_count;
        stats.uart_success_count += result.success ? 1U : 0U;
        if (!result.success) {
            stats.uart_errors++;
        }
        stats.uart_latency_last_ms = elapsed_ms;
        stats.uart_latency_max_ms = uart_poller_.maxLatencyMs();
        stats.uart_latency_avg_ms = uart_poller_.averageLatencyMs();
        stats.uart_poll_interval_current_ms = uart_poll_interval_ms_;
        stats_updated = true;
        xSemaphoreGive(statsMutex);
    }
    if (!stats_updated) {
        stats.uart_latency_last_ms = elapsed_ms;
        stats.uart_latency_max_ms = std::max(stats.uart_latency_max_ms, elapsed_ms);
        stats.uart_latency_avg_ms = uart_poller_.averageLatencyMs();
        stats.uart_poll_interval_current_ms = uart_poll_interval_ms_;
        stats.uart_retry_count += result.retries_performed;
        stats.uart_timeouts += result.timeout_count;
        stats.uart_crc_errors += result.crc_error_count;
        stats.uart_success_count += result.success ? 1U : 0U;
        if (!result.success) {
            stats.uart_errors++;
        }
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
        BridgeEventSink& event_sink = bridge->eventSink();
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - bridge->last_uart_poll_ms_ >= bridge->uart_poll_interval_ms_) {
            TinyBMS_LiveData d{};
            d.resetSnapshots();

            std::map<uint16_t, uint16_t> register_values;
            std::array<uint16_t, kTinyMaxReadWords> buffer{};
            bool read_success = true;

            for (size_t i = 0; i < kTinyReadBlockCount; ++i) {
                const auto& block = kTinyReadBlocks[i];
                std::fill(buffer.begin(), buffer.end(), 0);
                if (!bridge->readTinyRegisters(block.start, block.count, buffer.data())) {
                    read_success = false;
                    break;
                }

                for (uint16_t word = 0; word < block.count; ++word) {
                    register_values[block.start + word] = buffer[word];
                }
            }

            if (read_success) {
                // Phase 3: Collect MQTT register events to publish AFTER live_data
                std::vector<MqttRegisterEvent> deferred_mqtt_events;
                deferred_mqtt_events.reserve(32); // Reserve space for ~32 typical registers

                const auto& bindings = getTinyRegisterBindings();
                for (const auto& binding : bindings) {
                    if (binding.register_count == 0) {
                        continue;
                    }

                    uint16_t raw_words[TINY_REGISTER_MAX_WORDS] = {0};
                    bool has_all_words = true;
                    for (uint8_t idx = 0; idx < binding.register_count; ++idx) {
                        auto it = register_values.find(static_cast<uint16_t>(binding.register_address + idx));
                        if (it == register_values.end()) {
                            has_all_words = false;
                            break;
                        }
                        raw_words[idx] = it->second;
                    }

                    if (!has_all_words) {
                        continue;
                    }

                    int32_t raw_value = 0;
                    if (binding.value_type == TinyRegisterValueType::String) {
                        raw_value = 0;
                    } else if (binding.value_type == TinyRegisterValueType::Uint32 && binding.register_count >= 2) {
                        // TinyBMS exposes 32-bit counters as LSW/MSW pairs. Register `binding.register_address`
                        // holds the low word and the following register the high word (e.g. lifetime counter 32->33).
                        const uint32_t low_word = static_cast<uint32_t>(raw_words[0]);
                        const uint32_t high_word = static_cast<uint32_t>(raw_words[1]);
                        raw_value = static_cast<int32_t>((high_word << 16) | low_word);
                    } else if (binding.data_slice == TinyRegisterDataSlice::LowByte ||
                               binding.data_slice == TinyRegisterDataSlice::HighByte) {
                        const uint8_t byte_value = (binding.data_slice == TinyRegisterDataSlice::LowByte)
                            ? static_cast<uint8_t>(raw_words[0] & 0x00FFu)
                            : static_cast<uint8_t>((raw_words[0] >> 8) & 0x00FFu);
                        raw_value = binding.is_signed
                            ? static_cast<int32_t>(static_cast<int8_t>(byte_value))
                            : static_cast<int32_t>(byte_value);
                    } else {
                        raw_value = binding.is_signed
                            ? static_cast<int32_t>(static_cast<int16_t>(raw_words[0]))
                            : static_cast<int32_t>(raw_words[0]);
                    }

                    float scaled_value = static_cast<float>(raw_value) * binding.scale;

                    String text_value;
                    if (binding.value_type == TinyRegisterValueType::String) {
                        text_value.reserve(binding.register_count * 2);
                        for (uint8_t idx = 0; idx < binding.register_count; ++idx) {
                            char high = static_cast<char>((raw_words[idx] >> 8) & 0xFF);
                            char low = static_cast<char>(raw_words[idx] & 0xFF);
                            if (high != '\0') {
                                text_value += high;
                            }
                            if (low != '\0') {
                                text_value += low;
                            }
                        }
                    } else if (binding.metadata_address == 501 && binding.register_count >= 2) {
                        uint16_t major = raw_words[0];
                        uint16_t minor = raw_words[1];
                        text_value = String(major) + "." + String(minor);
                    }

                    const String* text_ptr = text_value.length() > 0 ? &text_value : nullptr;
                    d.applyBinding(binding, raw_value, scaled_value, text_ptr, raw_words);

                    // Phase 3: Defer MQTT register events - collect now, publish after live_data
                    if (event_sink.isReady()) {
                        MqttRegisterEvent mqtt_event{};
                        mqtt_event.address =
                            (binding.metadata_address != 0) ? binding.metadata_address : binding.register_address;
                        mqtt_event.value_type = binding.value_type;
                        mqtt_event.raw_value = raw_value;
                        mqtt_event.timestamp_ms = now;

                        const uint8_t copy_count = std::min<uint8_t>(binding.register_count,
                                                                      static_cast<uint8_t>(TINY_REGISTER_MAX_WORDS));
                        mqtt_event.raw_word_count = copy_count;
                        for (uint8_t i = 0; i < copy_count; ++i) {
                            mqtt_event.raw_words[i] = raw_words[i];
                        }
                        for (uint8_t i = copy_count; i < TINY_REGISTER_MAX_WORDS; ++i) {
                            mqtt_event.raw_words[i] = 0;
                        }

                        mqtt_event.has_text = (text_ptr != nullptr);
                        if (mqtt_event.has_text) {
                            size_t text_len = static_cast<size_t>(text_value.length());
                            if (text_len >= sizeof(mqtt_event.text_value)) {
                                text_len = sizeof(mqtt_event.text_value) - 1;
                            }
                            text_value.toCharArray(mqtt_event.text_value, text_len + 1);
                        } else {
                            mqtt_event.text_value[0] = '\0';
                        }

                        deferred_mqtt_events.push_back(mqtt_event);
                    }
                }

                d.cell_imbalance_mv = (d.max_cell_mv > d.min_cell_mv)
                    ? static_cast<uint16_t>(d.max_cell_mv - d.min_cell_mv)
                    : 0;

                if (d.online_status == 0) {
                    d.online_status = 0x91;
                }

                const bool has_pack_temp = (d.findSnapshot(113) != nullptr);
                const bool has_overvoltage_reg = (d.findSnapshot(315) != nullptr);
                const bool has_undervoltage_reg = (d.findSnapshot(316) != nullptr);
                const bool has_discharge_overcurrent_reg = (d.findSnapshot(317) != nullptr);
                const bool has_charge_overcurrent_reg = (d.findSnapshot(318) != nullptr);
                const bool has_overheat_reg = (d.findSnapshot(319) != nullptr);

                if (has_overvoltage_reg) {
                    bridge->config_.overvoltage_cutoff_mv = d.cell_overvoltage_mv;
                }
                if (has_undervoltage_reg) {
                    bridge->config_.undervoltage_cutoff_mv = d.cell_undervoltage_mv;
                }
                if (has_discharge_overcurrent_reg) {
                    bridge->config_.discharge_overcurrent_a = d.discharge_overcurrent_a;
                }
                if (has_charge_overcurrent_reg) {
                    bridge->config_.charge_overcurrent_a = d.charge_overcurrent_a;
                }
                if (has_overheat_reg) {
                    bridge->config_.overheat_cutoff_c = static_cast<float>(d.overheat_cutoff_c);
                }

                // Phase 1: Protect live_data_ write with liveMutex
                if (xSemaphoreTake(liveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    bridge->live_data_ = d;
                    xSemaphoreGive(liveMutex);
                } else {
                    logger.log(LOG_WARN, "[UART] Failed to acquire liveMutex for live_data_ write");
                }

                // Phase 3: Publish live_data FIRST to ensure consumers see complete snapshot
                LiveDataUpdate live_event{};
                live_event.metadata.source = EventSource::Uart;
                live_event.data = d;
                event_sink.publish(live_event);

                // Phase 3: Now publish deferred MQTT register events
                for (const auto& mqtt_event : deferred_mqtt_events) {
                    MqttRegisterValue mqtt_value{};
                    mqtt_value.metadata.source = EventSource::Uart;
                    mqtt_value.payload = mqtt_event;
                    event_sink.publish(mqtt_value);
                }

                // Phase 2: Protect config.victron.thresholds read
                VictronConfig::Thresholds th;
                if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    th = config.victron.thresholds;
                    xSemaphoreGive(configMutex);
                } else {
                    logger.log(LOG_WARN, "[UART] Failed to acquire configMutex for thresholds read, using defaults");
                    // Use safe defaults
                    th.overvoltage_v = 60.0f;
                    th.undervoltage_v = 40.0f;
                    th.overtemp_c = 60.0f;
                    th.low_temp_charge_c = 0.0f;
                }
                const float pack_voltage_v = d.voltage;
                const float internal_temp_c = d.temperature / 10.0f;
                const float pack_temp_max_c = has_pack_temp ? static_cast<float>(d.pack_temp_max) / 10.0f : internal_temp_c;
                const float pack_temp_min_c = has_pack_temp ? static_cast<float>(d.pack_temp_min) / 10.0f : internal_temp_c;
                const float overheat_cutoff_c = (has_overheat_reg && d.overheat_cutoff_c > 0)
                    ? static_cast<float>(d.overheat_cutoff_c)
                    : th.overtemp_c;

                float overvoltage_value = pack_voltage_v;
                bool overvoltage_alarm = false;
                if (has_overvoltage_reg && d.cell_overvoltage_mv > 0 && d.max_cell_mv > 0) {
                    overvoltage_value = static_cast<float>(d.max_cell_mv) / 1000.0f;
                    overvoltage_alarm = d.max_cell_mv >= d.cell_overvoltage_mv;
                } else {
                    overvoltage_alarm = pack_voltage_v > th.overvoltage_v;
                }
                if (overvoltage_alarm) {
                    publishAlarmEvent(event_sink,
                                       EventSource::Uart,
                                       AlarmCode::OverVoltage,
                                       "Voltage high",
                                       AlarmSeverity::Error,
                                       overvoltage_value);
                }

                float undervoltage_value = pack_voltage_v;
                bool undervoltage_alarm = false;
                if (has_undervoltage_reg && d.cell_undervoltage_mv > 0 && d.min_cell_mv > 0) {
                    undervoltage_value = static_cast<float>(d.min_cell_mv) / 1000.0f;
                    undervoltage_alarm = d.min_cell_mv <= d.cell_undervoltage_mv;
                } else {
                    undervoltage_alarm = (pack_voltage_v > 0.1f && pack_voltage_v < th.undervoltage_v);
                }
                if (undervoltage_alarm) {
                    publishAlarmEvent(event_sink,
                                       EventSource::Uart,
                                       AlarmCode::UnderVoltage,
                                       "Voltage low",
                                       AlarmSeverity::Warning,
                                       undervoltage_value);
                }

                if (d.cell_imbalance_mv > th.imbalance_alarm_mv) {
                    publishAlarmEvent(event_sink,
                                       EventSource::Uart,
                                       AlarmCode::CellImbalance,
                                       "Imbalance above alarm threshold",
                                       AlarmSeverity::Warning,
                                       static_cast<float>(d.cell_imbalance_mv));
                }
                if (pack_temp_max_c > overheat_cutoff_c) {
                    publishAlarmEvent(event_sink,
                                       EventSource::Uart,
                                       AlarmCode::OverTemperature,
                                       "Temp high",
                                       AlarmSeverity::Error,
                                       pack_temp_max_c);
                }
                if (pack_temp_min_c < th.low_temp_charge_c && d.current > 3.0f) {
                    publishAlarmEvent(event_sink,
                                       EventSource::Uart,
                                       AlarmCode::LowTempCharge,
                                       "Charging at low T",
                                       AlarmSeverity::Warning,
                                       pack_temp_min_c);
                }
            } else {
                publishAlarmEvent(event_sink,
                                   EventSource::Uart,
                                   AlarmCode::UartError,
                                   "TinyBMS UART error",
                                   AlarmSeverity::Warning,
                                   static_cast<float>(bridge->stats.uart_errors));
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
