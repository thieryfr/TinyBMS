#include "uart/tinybms_uart_client.h"

#include <algorithm>
#include <array>
#include <functional>

namespace {

constexpr uint8_t TINYBMS_PREAMBLE = 0xAA;
constexpr uint8_t CMD_READ_BLOCK = 0x07;
constexpr uint8_t CMD_READ_LIST  = 0x09;
constexpr uint8_t CMD_WRITE_BLOCK = 0x0B;
constexpr uint8_t CMD_WRITE_LIST  = 0x0D;
constexpr uint8_t CMD_ACK         = 0x01;
constexpr uint8_t CMD_NACK        = 0x81;

constexpr size_t MAX_FRAME_SIZE = 256;

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

using ResponseValidator = std::function<tinybms::AttemptStatus(const uint8_t*, size_t)>;

tinybms::TransactionResult performTransaction(hal::IHalUart& uart,
                                              const uint8_t* request,
                                              size_t request_len,
                                              size_t expected_response_len,
                                              const tinybms::TransactionOptions& options,
                                              const tinybms::DelayConfig& delay,
                                              const ResponseValidator& validator) {
    tinybms::TransactionResult result{};

    if (request == nullptr || request_len == 0 || expected_response_len > MAX_FRAME_SIZE) {
        result.last_status = tinybms::AttemptStatus::ProtocolError;
        return result;
    }

    const uint8_t attempts = std::max<uint8_t>(1, options.attempt_count);
    const uint32_t previous_timeout = uart.getTimeout();
    uart.setTimeout(options.response_timeout_ms);

    if (options.send_wakeup_pulse) {
        size_t warmup_written = uart.write(request, request_len);
        uart.flush();
        if (warmup_written != request_len) {
            result.write_error_count++;
        }
        performDelay(delay, options.wakeup_delay_ms);
        while (uart.available() > 0) {
            uart.read();
        }
    }

    std::array<uint8_t, MAX_FRAME_SIZE> response{};
    bool success = false;

    for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
        if (attempt > 0) {
            result.retries_performed++;
            performDelay(delay, options.retry_delay_ms);
        }

        while (uart.available() > 0) {
            uart.read();
        }

        size_t written = uart.write(request, request_len);
        uart.flush();
        if (written != request_len) {
            result.write_error_count++;
            result.last_status = tinybms::AttemptStatus::WriteError;
            continue;
        }

        size_t received = uart.readBytes(response.data(), expected_response_len);
        if (received != expected_response_len) {
            result.timeout_count++;
            result.last_status = tinybms::AttemptStatus::Timeout;
            continue;
        }

        const uint16_t resp_crc = static_cast<uint16_t>(response[received - 2]) |
                                  (static_cast<uint16_t>(response[received - 1]) << 8);
        const uint16_t calc_crc = modbusCRC16(response.data(), received - 2);
        if (resp_crc != calc_crc) {
            result.crc_error_count++;
            result.last_status = tinybms::AttemptStatus::CrcMismatch;
            continue;
        }

        tinybms::AttemptStatus status = validator(response.data(), received - 2);
        result.last_status = status;
        if (status == tinybms::AttemptStatus::Success) {
            success = true;
            break;
        }
    }

    uart.setTimeout(previous_timeout);
    result.success = success;
    if (!success && result.last_status == tinybms::AttemptStatus::Success) {
        result.last_status = tinybms::AttemptStatus::ProtocolError;
    }

    return result;
}

} // namespace

namespace tinybms {

TransactionResult readRegisterBlock(hal::IHalUart& uart,
                                    uint16_t start_addr,
                                    uint8_t register_count,
                                    uint16_t* output,
                                    const TransactionOptions& options,
                                    const DelayConfig& delay) {
    TransactionResult result{};

    if (output == nullptr || register_count == 0) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    const size_t expected_data_bytes = static_cast<size_t>(register_count) * 2U;
    const size_t frame_len_no_crc = 3 + 2; // AA 07 RL ADDR_L ADDR_H
    const size_t frame_len = frame_len_no_crc + 2;
    if (frame_len > MAX_FRAME_SIZE) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    std::array<uint8_t, MAX_FRAME_SIZE> request{};
    request[0] = TINYBMS_PREAMBLE;
    request[1] = CMD_READ_BLOCK;
    request[2] = register_count;
    request[3] = static_cast<uint8_t>(start_addr & 0xFF);
    request[4] = static_cast<uint8_t>((start_addr >> 8) & 0xFF);
    const uint16_t crc = modbusCRC16(request.data(), frame_len_no_crc);
    request[5] = static_cast<uint8_t>(crc & 0xFF);
    request[6] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    std::fill_n(output, register_count, static_cast<uint16_t>(0));

    const size_t expected_response_len = 3 + expected_data_bytes + 2;
    if (expected_response_len > MAX_FRAME_SIZE) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    auto validator = [output, register_count, expected_data_bytes](const uint8_t* data, size_t length_without_crc) {
        if (length_without_crc != 3 + expected_data_bytes) {
            return AttemptStatus::ProtocolError;
        }
        if (data[0] != TINYBMS_PREAMBLE || data[1] != CMD_READ_BLOCK) {
            return AttemptStatus::ProtocolError;
        }
        if (data[2] != expected_data_bytes) {
            return AttemptStatus::ProtocolError;
        }
        for (uint8_t i = 0; i < register_count; ++i) {
            const size_t idx = 3 + static_cast<size_t>(i) * 2U;
            output[i] = static_cast<uint16_t>(data[idx]) |
                        (static_cast<uint16_t>(data[idx + 1]) << 8);
        }
        return AttemptStatus::Success;
    };

    result = performTransaction(uart, request.data(), frame_len, expected_response_len, options, delay, validator);
    return result;
}

TransactionResult readIndividualRegisters(hal::IHalUart& uart,
                                         const uint16_t* addresses,
                                         size_t address_count,
                                         uint16_t* output,
                                         const TransactionOptions& options,
                                         const DelayConfig& delay) {
    TransactionResult result{};

    if (addresses == nullptr || output == nullptr || address_count == 0) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    const size_t payload_len = address_count * 2U;
    const size_t frame_len_no_crc = 3 + payload_len;
    const size_t frame_len = frame_len_no_crc + 2;
    if (frame_len > MAX_FRAME_SIZE) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    std::array<uint8_t, MAX_FRAME_SIZE> request{};
    request[0] = TINYBMS_PREAMBLE;
    request[1] = CMD_READ_LIST;
    request[2] = static_cast<uint8_t>(payload_len & 0xFF);

    for (size_t i = 0; i < address_count; ++i) {
        const size_t offset = 3 + i * 2U;
        request[offset]     = static_cast<uint8_t>(addresses[i] & 0xFF);
        request[offset + 1] = static_cast<uint8_t>((addresses[i] >> 8) & 0xFF);
    }

    const uint16_t crc = modbusCRC16(request.data(), frame_len_no_crc);
    request[frame_len_no_crc]     = static_cast<uint8_t>(crc & 0xFF);
    request[frame_len_no_crc + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    std::fill_n(output, address_count, static_cast<uint16_t>(0));

    const size_t expected_response_len = 3 + payload_len + 2;
    if (expected_response_len > MAX_FRAME_SIZE) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    auto validator = [output, address_count, payload_len](const uint8_t* data, size_t length_without_crc) {
        if (length_without_crc != 3 + payload_len) {
            return AttemptStatus::ProtocolError;
        }
        if (data[0] != TINYBMS_PREAMBLE || data[1] != CMD_READ_LIST) {
            return AttemptStatus::ProtocolError;
        }
        if (data[2] != payload_len) {
            return AttemptStatus::ProtocolError;
        }
        for (size_t i = 0; i < address_count; ++i) {
            const size_t idx = 3 + i * 2U;
            output[i] = static_cast<uint16_t>(data[idx]) |
                        (static_cast<uint16_t>(data[idx + 1]) << 8);
        }
        return AttemptStatus::Success;
    };

    result = performTransaction(uart, request.data(), frame_len, expected_response_len, options, delay, validator);
    return result;
}

TransactionResult writeRegisterBlock(hal::IHalUart& uart,
                                     uint16_t start_addr,
                                     const uint16_t* values,
                                     size_t value_count,
                                     const TransactionOptions& options,
                                     const DelayConfig& delay) {
    TransactionResult result{};

    if (values == nullptr || value_count == 0) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    const size_t payload_len = value_count * 2U;
    const size_t frame_len_no_crc = 3 + 2 + payload_len;
    const size_t frame_len = frame_len_no_crc + 2;
    if (frame_len > MAX_FRAME_SIZE) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    std::array<uint8_t, MAX_FRAME_SIZE> request{};
    request[0] = TINYBMS_PREAMBLE;
    request[1] = CMD_WRITE_BLOCK;
    request[2] = static_cast<uint8_t>(payload_len & 0xFF);
    request[3] = static_cast<uint8_t>(start_addr & 0xFF);
    request[4] = static_cast<uint8_t>((start_addr >> 8) & 0xFF);

    for (size_t i = 0; i < value_count; ++i) {
        const size_t offset = 5 + i * 2U;
        request[offset]     = static_cast<uint8_t>(values[i] & 0xFF);
        request[offset + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
    }

    const uint16_t crc = modbusCRC16(request.data(), frame_len_no_crc);
    request[frame_len_no_crc]     = static_cast<uint8_t>(crc & 0xFF);
    request[frame_len_no_crc + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    constexpr size_t expected_response_len = 5; // AA 01 STATUS CRC_L CRC_H

    auto validator = [](const uint8_t* data, size_t length_without_crc) {
        if (length_without_crc != 3) {
            return AttemptStatus::ProtocolError;
        }
        if (data[0] != TINYBMS_PREAMBLE) {
            return AttemptStatus::ProtocolError;
        }
        if (data[1] == CMD_ACK && data[2] == 0x00) {
            return AttemptStatus::Success;
        }
        if (data[1] == CMD_NACK) {
            return AttemptStatus::ProtocolError;
        }
        return AttemptStatus::ProtocolError;
    };

    result = performTransaction(uart, request.data(), frame_len, expected_response_len, options, delay, validator);
    return result;
}

TransactionResult writeIndividualRegisters(hal::IHalUart& uart,
                                          const uint16_t* addresses,
                                          const uint16_t* values,
                                          size_t pair_count,
                                          const TransactionOptions& options,
                                          const DelayConfig& delay) {
    TransactionResult result{};

    if (addresses == nullptr || values == nullptr || pair_count == 0) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    const size_t payload_len = pair_count * 4U;
    const size_t frame_len_no_crc = 3 + payload_len;
    const size_t frame_len = frame_len_no_crc + 2;
    if (frame_len > MAX_FRAME_SIZE) {
        result.last_status = AttemptStatus::ProtocolError;
        return result;
    }

    std::array<uint8_t, MAX_FRAME_SIZE> request{};
    request[0] = TINYBMS_PREAMBLE;
    request[1] = CMD_WRITE_LIST;
    request[2] = static_cast<uint8_t>(payload_len & 0xFF);

    for (size_t i = 0; i < pair_count; ++i) {
        const size_t offset = 3 + i * 4U;
        request[offset]     = static_cast<uint8_t>(addresses[i] & 0xFF);
        request[offset + 1] = static_cast<uint8_t>((addresses[i] >> 8) & 0xFF);
        request[offset + 2] = static_cast<uint8_t>(values[i] & 0xFF);
        request[offset + 3] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
    }

    const uint16_t crc = modbusCRC16(request.data(), frame_len_no_crc);
    request[frame_len_no_crc]     = static_cast<uint8_t>(crc & 0xFF);
    request[frame_len_no_crc + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    constexpr size_t expected_response_len = 5; // AA 01 STATUS CRC_L CRC_H

    auto validator = [](const uint8_t* data, size_t length_without_crc) {
        if (length_without_crc != 3) {
            return AttemptStatus::ProtocolError;
        }
        if (data[0] != TINYBMS_PREAMBLE) {
            return AttemptStatus::ProtocolError;
        }
        if (data[1] == CMD_ACK && data[2] == 0x00) {
            return AttemptStatus::Success;
        }
        if (data[1] == CMD_NACK) {
            return AttemptStatus::ProtocolError;
        }
        return AttemptStatus::ProtocolError;
    };

    result = performTransaction(uart, request.data(), frame_len, expected_response_len, options, delay, validator);
    return result;
}

} // namespace tinybms

