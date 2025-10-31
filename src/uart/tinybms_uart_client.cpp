#include "uart/tinybms_uart_client.h"

#include <algorithm>
#include <array>

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

void performDelay(const tinybms::DelayConfig& delay, uint32_t delay_ms) {
    if (delay.delay_fn && delay_ms > 0) {
        delay.delay_fn(delay_ms, delay.context);
    }
}

} // namespace

namespace tinybms {

TransactionResult readHoldingRegisters(hal::IHalUart& uart,
                                       uint16_t start_addr,
                                       uint16_t count,
                                       uint16_t* output,
                                       const TransactionOptions& options,
                                       const DelayConfig& delay) {
    TransactionResult result{};

    if (output == nullptr || count == 0) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    const size_t expected_data_bytes = static_cast<size_t>(count) * 2U;
    const size_t expected_response_len = 3 + expected_data_bytes + 2;
    const bool include_start_byte = options.include_start_byte;
    const bool send_wakeup_pulse = options.send_wakeup_pulse;
    if (expected_response_len + (include_start_byte ? 1U : 0U) > 256) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    std::fill_n(output, count, static_cast<uint16_t>(0));

    const uint8_t attempts = std::max<uint8_t>(1, options.attempt_count);
    const uint32_t previous_timeout = uart.getTimeout();
    uart.setTimeout(options.response_timeout_ms);

    std::array<uint8_t, 8> base_request{};
    base_request[0] = TINYBMS_SLAVE_ADDRESS;
    base_request[1] = MODBUS_READ_HOLDING_REGS;
    base_request[2] = static_cast<uint8_t>((start_addr >> 8) & 0xFF);
    base_request[3] = static_cast<uint8_t>(start_addr & 0xFF);
    base_request[4] = static_cast<uint8_t>((count >> 8) & 0xFF);
    base_request[5] = static_cast<uint8_t>(count & 0xFF);
    const uint16_t crc = modbusCRC16(base_request.data(), 6);
    base_request[6] = static_cast<uint8_t>(crc & 0xFF);
    base_request[7] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    std::array<uint8_t, 9> request{};
    size_t request_len = base_request.size();
    size_t copy_offset = 0;
    if (include_start_byte) {
        request[0] = 0xAA;
        copy_offset = 1;
        request_len += 1;
    }
    std::copy(base_request.begin(), base_request.end(), request.begin() + copy_offset);

    bool success = false;

    if (send_wakeup_pulse) {
        size_t warmup_written = uart.write(request.data(), request_len);
        uart.flush();
        if (warmup_written != request_len) {
            result.write_error_count++;
        }
        performDelay(delay, options.wakeup_delay_ms);
        while (uart.available() > 0) {
            uart.read();
        }
    }

    for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
        if (attempt > 0) {
            result.retries_performed++;
            performDelay(delay, options.retry_delay_ms);
        }

        while (uart.available() > 0) {
            uart.read();
        }

        size_t written = uart.write(request.data(), request_len);
        uart.flush();
        if (written != request_len) {
            result.write_error_count++;
            result.last_status = AttemptStatus::WriteError;
            continue;
        }

        std::array<uint8_t, 256> response{};
        const size_t read_len = expected_response_len + (include_start_byte ? 1U : 0U);
        size_t received = uart.readBytes(response.data(), read_len);
        if (include_start_byte && received == read_len && response[0] == 0xAA) {
            for (size_t i = 0; i + 1 < received; ++i) {
                response[i] = response[i + 1];
            }
            received -= 1;
        }
        if (received != expected_response_len) {
            result.timeout_count++;
            result.last_status = AttemptStatus::Timeout;
            continue;
        }

        const uint16_t resp_crc = static_cast<uint16_t>(response[received - 2]) |
                                  (static_cast<uint16_t>(response[received - 1]) << 8);
        const uint16_t calc_crc = modbusCRC16(response.data(), received - 2);
        if (resp_crc != calc_crc) {
            result.crc_error_count++;
            result.last_status = AttemptStatus::CrcMismatch;
            continue;
        }

        if (response[0] != TINYBMS_SLAVE_ADDRESS || response[1] != MODBUS_READ_HOLDING_REGS) {
            result.last_status = AttemptStatus::ProtocolError;
            continue;
        }

        if (response[2] != expected_data_bytes) {
            result.last_status = AttemptStatus::ProtocolError;
            continue;
        }

        for (uint16_t i = 0; i < count; ++i) {
            const size_t idx = 3 + i * 2;
            output[i] = static_cast<uint16_t>((response[idx] << 8) | response[idx + 1]);
        }

        success = true;
        result.last_status = AttemptStatus::Success;
        break;
    }

    uart.setTimeout(previous_timeout);
    result.success = success;
    if (!success && result.last_status == AttemptStatus::ProtocolError) {
        // No successful attempt and no specific error recorded, default to protocol error
        result.last_status = AttemptStatus::ProtocolError;
    }

    return result;
}

} // namespace tinybms
