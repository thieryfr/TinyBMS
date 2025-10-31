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

std::vector<uint8_t> buildReadBlockRequest(uint16_t start_addr, uint8_t count) {
    std::vector<uint8_t> request(7, 0);
    request[0] = 0xAA;
    request[1] = 0x07;
    request[2] = count;
    request[3] = static_cast<uint8_t>(start_addr & 0xFF);
    request[4] = static_cast<uint8_t>((start_addr >> 8) & 0xFF);
    const uint16_t crc = modbusCRC16(request.data(), 5);
    request[5] = static_cast<uint8_t>(crc & 0xFF);
    request[6] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return request;
}

std::vector<uint8_t> buildReadBlockResponse(const std::vector<uint16_t>& values) {
    const uint8_t payload_len = static_cast<uint8_t>(values.size() * 2U);
    std::vector<uint8_t> response(3 + payload_len + 2, 0);
    response[0] = 0xAA;
    response[1] = 0x07;
    response[2] = payload_len;
    for (size_t i = 0; i < values.size(); ++i) {
        const size_t idx = 3 + i * 2U;
        response[idx]     = static_cast<uint8_t>(values[i] & 0xFF);
        response[idx + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
    }
    const uint16_t crc = modbusCRC16(response.data(), response.size() - 2);
    response[response.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
    response[response.size() - 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return response;
}

std::vector<uint8_t> buildReadListRequest(const std::vector<uint16_t>& addresses) {
    const uint8_t payload_len = static_cast<uint8_t>(addresses.size() * 2U);
    std::vector<uint8_t> request(3 + payload_len + 2, 0);
    request[0] = 0xAA;
    request[1] = 0x09;
    request[2] = payload_len;
    for (size_t i = 0; i < addresses.size(); ++i) {
        const size_t idx = 3 + i * 2U;
        request[idx]     = static_cast<uint8_t>(addresses[i] & 0xFF);
        request[idx + 1] = static_cast<uint8_t>((addresses[i] >> 8) & 0xFF);
    }
    const uint16_t crc = modbusCRC16(request.data(), request.size() - 2);
    request[request.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
    request[request.size() - 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return request;
}

std::vector<uint8_t> buildReadListResponse(const std::vector<uint16_t>& values) {
    const uint8_t payload_len = static_cast<uint8_t>(values.size() * 2U);
    std::vector<uint8_t> response(3 + payload_len + 2, 0);
    response[0] = 0xAA;
    response[1] = 0x09;
    response[2] = payload_len;
    for (size_t i = 0; i < values.size(); ++i) {
        const size_t idx = 3 + i * 2U;
        response[idx]     = static_cast<uint8_t>(values[i] & 0xFF);
        response[idx + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
    }
    const uint16_t crc = modbusCRC16(response.data(), response.size() - 2);
    response[response.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
    response[response.size() - 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return response;
}

std::vector<uint8_t> buildWriteListRequest(const std::vector<uint16_t>& addresses,
                                           const std::vector<uint16_t>& values) {
    const uint8_t payload_len = static_cast<uint8_t>(addresses.size() * 4U);
    std::vector<uint8_t> request(3 + payload_len + 2, 0);
    request[0] = 0xAA;
    request[1] = 0x0D;
    request[2] = payload_len;
    for (size_t i = 0; i < addresses.size(); ++i) {
        const size_t idx = 3 + i * 4U;
        request[idx]     = static_cast<uint8_t>(addresses[i] & 0xFF);
        request[idx + 1] = static_cast<uint8_t>((addresses[i] >> 8) & 0xFF);
        request[idx + 2] = static_cast<uint8_t>(values[i] & 0xFF);
        request[idx + 3] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
    }
    const uint16_t crc = modbusCRC16(request.data(), request.size() - 2);
    request[request.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
    request[request.size() - 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return request;
}

std::vector<uint8_t> buildAckResponse(uint8_t status = 0x00) {
    std::vector<uint8_t> response{0xAA, 0x01, status, 0x00, 0x00};
    const uint16_t crc = modbusCRC16(response.data(), response.size() - 2);
    response[3] = static_cast<uint8_t>(crc & 0xFF);
    response[4] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return response;
}

} // namespace

int main() {
    {
        TinyBmsUartStub stub;
        const uint16_t start = 0x012C;
        const uint8_t count = 2;
        std::vector<uint16_t> values{0x1234, 0x5678};
        auto request = buildReadBlockRequest(start, count);
        auto response = buildReadBlockResponse(values);

        TinyBmsUartStub::Exchange exchange{};
        exchange.expected_request = request;
        exchange.response = response;
        stub.queueExchange(std::move(exchange));

        uint16_t output[2] = {};
        tinybms::TransactionOptions options{};
        options.attempt_count = 1;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 50;

        tinybms::TransactionResult result = tinybms::readRegisterBlock(
            stub, start, count, output, options);

        assert(result.success);
        assert(result.retries_performed == 0);
        assert(result.timeout_count == 0);
        assert(result.crc_error_count == 0);
        assert(result.write_error_count == 0);
        assert(stub.lastRequestMatchesExpected());
        for (uint8_t i = 0; i < count; ++i) {
            assert(output[i] == values[i]);
        }
    }

    {
        TinyBmsUartStub stub;
        std::vector<uint16_t> addresses{0x0020, 0x0133, 0x01F4};
        std::vector<uint16_t> values{0xAAAA, 0xBBBB, 0xCCCC};
        auto request = buildReadListRequest(addresses);
        auto response = buildReadListResponse(values);

        TinyBmsUartStub::Exchange exchange{};
        exchange.expected_request = request;
        exchange.response = response;
        stub.queueExchange(std::move(exchange));

        uint16_t output[3] = {};
        tinybms::TransactionOptions options{};
        options.attempt_count = 1;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 50;
        options.send_wakeup_pulse = true;
        options.wakeup_delay_ms = 0;

        tinybms::TransactionResult result = tinybms::readIndividualRegisters(
            stub, addresses.data(), addresses.size(), output, options);

        assert(result.success);
        assert(result.retries_performed == 0);
        assert(result.timeout_count == 0);
        assert(result.crc_error_count == 0);
        assert(result.write_error_count == 0);
        assert(stub.lastRequestMatchesExpected());
        for (size_t i = 0; i < addresses.size(); ++i) {
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
        std::vector<uint16_t> addresses{0x0100, 0x0101};
        tinybms::TransactionOptions options{};
        options.attempt_count = 2;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 10;

        tinybms::TransactionResult result = tinybms::readIndividualRegisters(
            stub, addresses.data(), addresses.size(), output, options);

        assert(!result.success);
        assert(result.retries_performed == 1);
        assert(result.timeout_count == 2);
        assert(result.crc_error_count == 0);
        assert(result.last_status == tinybms::AttemptStatus::Timeout);
    }

    {
        TinyBmsUartStub stub;
        std::vector<uint16_t> addresses{0x0200};
        auto request = buildReadListRequest(addresses);
        auto response = buildReadListResponse({0x0F0F});
        response[response.size() - 1] ^= 0xFF; // Corrupt CRC

        TinyBmsUartStub::Exchange exchange{};
        exchange.expected_request = request;
        exchange.response = response;
        stub.queueExchange(std::move(exchange));

        uint16_t output[1] = {0};
        tinybms::TransactionOptions options{};
        options.attempt_count = 1;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 20;

        tinybms::TransactionResult result = tinybms::readIndividualRegisters(
            stub, addresses.data(), addresses.size(), output, options);

        assert(!result.success);
        assert(result.crc_error_count == 1);
        assert(result.last_status == tinybms::AttemptStatus::CrcMismatch);
        assert(stub.lastRequestMatchesExpected());
    }

    {
        TinyBmsUartStub stub;
        std::vector<uint16_t> addresses{0x0300};
        std::vector<uint16_t> values{0x1357};
        auto request = buildWriteListRequest(addresses, values);
        auto response = buildAckResponse();

        TinyBmsUartStub::Exchange exchange{};
        exchange.expected_request = request;
        exchange.response = response;
        stub.queueExchange(std::move(exchange));

        tinybms::TransactionOptions options{};
        options.attempt_count = 1;
        options.retry_delay_ms = 0;
        options.response_timeout_ms = 50;

        tinybms::TransactionResult result = tinybms::writeIndividualRegisters(
            stub, addresses.data(), values.data(), addresses.size(), options);

        assert(result.success);
        assert(result.retries_performed == 0);
        assert(result.timeout_count == 0);
        assert(result.crc_error_count == 0);
        assert(result.write_error_count == 0);
        assert(stub.lastRequestMatchesExpected());
    }

    return 0;
}

