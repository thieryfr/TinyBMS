#include <Arduino.h>
#include "uart/uart_channel.h"

namespace {

class HardwareSerialChannel : public IUartChannel {
public:
    explicit HardwareSerialChannel(HardwareSerial& serial) : serial_(serial), timeout_ms_(1000) {}

    void begin(unsigned long baudrate,
               uint32_t config,
               int8_t rx_pin,
               int8_t tx_pin) override {
        serial_.begin(baudrate, config, rx_pin, tx_pin);
    }

    void setTimeout(uint32_t timeout_ms) override {
        timeout_ms_ = timeout_ms;
        serial_.setTimeout(timeout_ms);
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
    uint32_t timeout_ms_;
};

} // namespace

IUartChannel& defaultTinyBmsUart() {
    static HardwareSerialChannel instance(Serial1);
    return instance;
}
