#include <cassert>
#include <cstdint>
#include <vector>
#include "uart/tinybms_uart_client.h"
#include "uart_stub.h"

namespace {

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

std::vector<uint8_t> buildRequest(uint16_t start_addr, uint16_t count, bool include_start_byte = false) {
    std::vector<uint8_t> request(include_start_byte ? 9 : 8, 0);
    size_t offset = 0;
    if (include_start_byte) {
        request[0] = 0xAA;
        offset = 1;
    }
    request[offset + 0] = 0x01;
    request[offset + 1] = 0x03;
    request[offset + 2] = static_cast<uint8_t>((start_addr >> 8) & 0xFF);
    request[offset + 3] = static_cast<uint8_t>(start_addr & 0xFF);
    request[offset + 4] = static_cast<uint8_t>((count >> 8) & 0xFF);
    request[offset + 5] = static_cast<uint8_t>(count & 0xFF);
    const uint16_t crc = modbusCRC16(request.data() + offset, 6);
    request[offset + 6] = static_cast<uint8_t>(crc & 0xFF);
    request[offset + 7] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return request;
}

std::vector<uint8_t> buildResponse(uint16_t count, const std::vector<uint16_t>& values) {
    const size_t byte_count = static_cast<size_t>(count) * 2;
    std::vector<uint8_t> response(3 + byte_count + 2, 0);
    response[0] = 0x01;
    response[1] = 0x03;
    response[2] = static_cast<uint8_t>(byte_count);
    for (uint16_t i = 0; i < count; ++i) {
        response[3 + i * 2] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
        response[4 + i * 2] = static_cast<uint8_t>(values[i] & 0xFF);
    }
    const uint16_t crc = modbusCRC16(response.data(), response.size() - 2);
    response[response.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
    response[response.size() - 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return response;
}

} // namespace

int main() {
    {
        TinyBmsUartStub stub;
        const uint16_t start = 0x0100;
        const uint16_t count = 3;
        std::vector<uint16_t> values{0x1234, 0x5678, 0x9ABC};
        auto request = buildRequest(start, count);
        auto response = buildResponse(count, values);

        TinyBmsUartStub::Exchange exchange{};
        exchange.expected_request = request;
        exchange.response = response;
        stub.queueExchange(std::move(exchange));

        uint16_t output[3] = {};
        tinybms::TransactionOptions options{};
        options.attempt_count = 1;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 50;

        tinybms::TransactionResult result = tinybms::readHoldingRegisters(
            stub, start, count, output, options);

        assert(result.success);
        assert(result.retries_performed == 0);
        assert(result.timeout_count == 0);
        assert(result.crc_error_count == 0);
        assert(result.write_error_count == 0);
        assert(stub.lastRequestMatchesExpected());
        for (uint16_t i = 0; i < count; ++i) {
            assert(output[i] == values[i]);
        }
    }

    {
        TinyBmsUartStub stub;
        const uint16_t start = 0x0110;
        const uint16_t count = 2;
        std::vector<uint16_t> values{0xAAAA, 0xBBBB};
        auto request = buildRequest(start, count, true);
        auto response = buildResponse(count, values);

        TinyBmsUartStub::Exchange exchange{};
        exchange.expected_request = request;
        exchange.response = response;
        stub.queueExchange(std::move(exchange));

        uint16_t output[2] = {};
        tinybms::TransactionOptions options{};
        options.attempt_count = 1;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 50;
        options.include_start_byte = true;
        options.send_wakeup_pulse = true;
        options.wakeup_delay_ms = 0;

        tinybms::TransactionResult result = tinybms::readHoldingRegisters(
            stub, start, count, output, options);

        assert(result.success);
        assert(result.retries_performed == 0);
        assert(result.timeout_count == 0);
        assert(result.crc_error_count == 0);
        assert(result.write_error_count == 0);
        assert(stub.lastRequestMatchesExpected());
        for (uint16_t i = 0; i < count; ++i) {
            assert(output[i] == values[i]);
        }
    }

    {
        TinyBmsUartStub stub;
        TinyBmsUartStub::Exchange first{};
        first.drop_response = true;
        TinyBmsUartStub::Exchange second{};
        second.drop_response = true;
        stub.queueExchange(first);
        stub.queueExchange(second);

        uint16_t output[2] = {};
        tinybms::TransactionOptions options{};
        options.attempt_count = 2;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 10;

        tinybms::TransactionResult result = tinybms::readHoldingRegisters(
            stub, 0x0200, 2, output, options);

        assert(!result.success);
        assert(result.retries_performed == 1);
        assert(result.timeout_count == 2);
        assert(result.crc_error_count == 0);
        assert(result.last_status == tinybms::AttemptStatus::Timeout);
    }

    {
        TinyBmsUartStub stub;
        auto request = buildRequest(0x0300, 1);
        auto response = buildResponse(1, {0x0F0F});
        // Corrupt CRC
        response[response.size() - 1] ^= 0xFF;

        TinyBmsUartStub::Exchange exchange{};
        exchange.expected_request = request;
        exchange.response = response;
        stub.queueExchange(std::move(exchange));

        uint16_t output[1] = {0};
        tinybms::TransactionOptions options{};
        options.attempt_count = 1;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 20;

        tinybms::TransactionResult result = tinybms::readHoldingRegisters(
            stub, 0x0300, 1, output, options);

        assert(!result.success);
        assert(result.crc_error_count == 1);
        assert(result.last_status == tinybms::AttemptStatus::CrcMismatch);
        assert(stub.lastRequestMatchesExpected());
    }

    return 0;
}
