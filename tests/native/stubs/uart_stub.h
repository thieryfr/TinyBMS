#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>
#include "hal/interfaces/ihal_uart.h"

class TinyBmsUartStub : public hal::IHalUart {
public:
    struct Exchange {
        std::vector<uint8_t> expected_request;
        std::vector<uint8_t> response;
        bool drop_response = false;
    };

    TinyBmsUartStub() : timeout_ms_(100), available_bytes_(0), read_index_(0) {}

    hal::Status initialize(const hal::UartConfig& config) override {
        timeout_ms_ = config.timeout_ms;
        return hal::Status::Ok;
    }

    void setTimeout(uint32_t timeout_ms) override { timeout_ms_ = timeout_ms; }
    uint32_t getTimeout() const override { return timeout_ms_; }

    size_t write(const uint8_t* buffer, size_t size) override {
        last_write_.assign(buffer, buffer + size);

        if (!exchanges_.empty()) {
            const auto& exchange = exchanges_.front();
            if (!exchange.expected_request.empty()) {
                last_request_matches_ = (exchange.expected_request == last_write_);
            } else {
                last_request_matches_ = true;
            }

            if (exchange.drop_response) {
                active_response_.clear();
                available_bytes_ = 0;
                read_index_ = 0;
            } else {
                active_response_ = exchange.response;
                available_bytes_ = active_response_.size();
                read_index_ = 0;
            }
        } else {
            last_request_matches_ = false;
            active_response_.clear();
            available_bytes_ = 0;
            read_index_ = 0;
        }

        return size;
    }

    void flush() override {}

    size_t readBytes(uint8_t* buffer, size_t length) override {
        if (exchanges_.empty()) {
            return 0;
        }

        const auto exchange = exchanges_.front();
        exchanges_.pop();

        if (exchange.drop_response) {
            available_bytes_ = 0;
            read_index_ = 0;
            active_response_.clear();
            return 0;
        }

        size_t remaining = active_response_.size() - read_index_;
        size_t to_copy = std::min(length, remaining);
        for (size_t i = 0; i < to_copy; ++i) {
            buffer[i] = active_response_[read_index_ + i];
        }
        read_index_ += to_copy;
        available_bytes_ = remaining > to_copy ? remaining - to_copy : 0;
        return to_copy;
    }

    int available() override {
        return static_cast<int>(available_bytes_);
    }

    int read() override {
        if (available_bytes_ == 0 || read_index_ >= active_response_.size()) {
            return -1;
        }
        uint8_t value = active_response_[read_index_++];
        --available_bytes_;
        return value;
    }

    void queueExchange(Exchange exchange) {
        exchanges_.push(std::move(exchange));
    }

    const std::vector<uint8_t>& lastWrite() const { return last_write_; }
    bool lastRequestMatchesExpected() const { return last_request_matches_; }

private:
    std::queue<Exchange> exchanges_;
    std::vector<uint8_t> active_response_;
    std::vector<uint8_t> last_write_;
    bool last_request_matches_ = false;
    uint32_t timeout_ms_;
    size_t available_bytes_;
    size_t read_index_;
};
