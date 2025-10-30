#include "hal/interfaces/ihal_uart.h"

#include <deque>
#include <memory>
#include <vector>

namespace hal {

class MockUart : public IHalUart {
public:
    Status initialize(const UartConfig& config) override {
        config_ = config;
        timeout_ms_ = config.timeout_ms;
        initialized_ = true;
        return Status::Ok;
    }

    void setTimeout(uint32_t timeout_ms) override { timeout_ms_ = timeout_ms; }
    uint32_t getTimeout() const override { return timeout_ms_; }

    size_t write(const uint8_t* buffer, size_t size) override {
        for (size_t i = 0; i < size; ++i) {
            tx_buffer_.push_back(buffer[i]);
        }
        return size;
    }

    void flush() override {}

    size_t readBytes(uint8_t* buffer, size_t length) override {
        size_t read = 0;
        while (read < length && !rx_buffer_.empty()) {
            buffer[read++] = rx_buffer_.front();
            rx_buffer_.pop_front();
        }
        return read;
    }

    int available() override { return static_cast<int>(rx_buffer_.size()); }

    int read() override {
        if (rx_buffer_.empty()) {
            return -1;
        }
        int value = rx_buffer_.front();
        rx_buffer_.pop_front();
        return value;
    }

    const std::vector<uint8_t>& writtenData() const { return tx_buffer_; }
    void pushRx(const std::vector<uint8_t>& data) {
        for (uint8_t b : data) {
            rx_buffer_.push_back(b);
        }
    }

private:
    UartConfig config_{};
    uint32_t timeout_ms_ = 0;
    bool initialized_ = false;
    std::deque<uint8_t> rx_buffer_;
    std::vector<uint8_t> tx_buffer_;
};

std::unique_ptr<IHalUart> createMockUart() {
    return std::make_unique<MockUart>();
}

} // namespace hal
