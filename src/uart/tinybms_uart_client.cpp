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
    if (expected_response_len > 256) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    std::fill_n(output, count, static_cast<uint16_t>(0));

    const uint8_t attempts = std::max<uint8_t>(1, options.attempt_count);
    const uint32_t previous_timeout = uart.getTimeout();
    uart.setTimeout(options.response_timeout_ms);

    std::array<uint8_t, 8> request{};
    request[0] = TINYBMS_SLAVE_ADDRESS;
    request[1] = MODBUS_READ_HOLDING_REGS;
    request[2] = static_cast<uint8_t>((start_addr >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(start_addr & 0xFF);
    request[4] = static_cast<uint8_t>((count >> 8) & 0xFF);
    request[5] = static_cast<uint8_t>(count & 0xFF);
    const uint16_t crc = modbusCRC16(request.data(), 6);
    request[6] = static_cast<uint8_t>(crc & 0xFF);
    request[7] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    bool success = false;

    for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
        if (attempt > 0) {
            result.retries_performed++;
            performDelay(delay, options.retry_delay_ms);
        }

        while (uart.available() > 0) {
            uart.read();
        }

        size_t written = uart.write(request.data(), request.size());
        uart.flush();
        if (written != request.size()) {
            result.write_error_count++;
            result.last_status = AttemptStatus::WriteError;
            continue;
        }

        std::array<uint8_t, 256> response{};
        size_t received = uart.readBytes(response.data(), expected_response_len);
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
