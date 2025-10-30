#include "hal/interfaces/ihal_uart.h"

#include <HardwareSerial.h>

namespace hal {

namespace {

class Esp32Uart : public IHalUart {
public:
    Esp32Uart() : serial_(Serial1) {}

    Status initialize(const UartConfig& config) override {
        serial_.begin(config.baudrate, SERIAL_8N1, config.rx_pin, config.tx_pin, config.use_dma);
        timeout_ms_ = config.timeout_ms;
        serial_.setTimeout(timeout_ms_);
        return Status::Ok;
    }

    void setTimeout(uint32_t timeout_ms) override {
        timeout_ms_ = timeout_ms;
        serial_.setTimeout(timeout_ms_);
    }

    uint32_t getTimeout() const override {
        return timeout_ms_;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        return serial_.write(buffer, size);
    }

    void flush() override {
        serial_.flush();
    }

    size_t readBytes(uint8_t* buffer, size_t length) override {
        return serial_.readBytes(buffer, length);
    }

    int available() override {
        return serial_.available();
    }

    int read() override {
        return serial_.read();
    }

private:
    HardwareSerial& serial_;
    uint32_t timeout_ms_ = 1000;
};

} // namespace

std::unique_ptr<IHalUart> createEsp32Uart() {
    return std::make_unique<Esp32Uart>();
}

} // namespace hal
