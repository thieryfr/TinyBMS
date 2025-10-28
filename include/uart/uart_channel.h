#pragma once

#include <cstddef>
#include <cstdint>

class IUartChannel {
public:
    virtual ~IUartChannel() = default;

    virtual void begin(unsigned long baudrate,
                       uint32_t config,
                       int8_t rx_pin,
                       int8_t tx_pin) = 0;

    virtual void setTimeout(uint32_t timeout_ms) = 0;
    virtual uint32_t getTimeout() const = 0;

    virtual size_t write(const uint8_t* buffer, size_t size) = 0;
    virtual void flush() = 0;

    virtual size_t readBytes(uint8_t* buffer, size_t length) = 0;
    virtual int available() = 0;
    virtual int read() = 0;
};

IUartChannel& defaultTinyBmsUart();
