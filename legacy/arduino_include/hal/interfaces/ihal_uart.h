#pragma once

#include <cstddef>
#include <cstdint>
#include "hal/hal_config.h"
#include "hal/hal_types.h"

namespace hal {

class IHalUart {
public:
    virtual ~IHalUart() = default;

    virtual Status initialize(const UartConfig& config) = 0;
    virtual void setTimeout(uint32_t timeout_ms) = 0;
    virtual uint32_t getTimeout() const = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) = 0;
    virtual void flush() = 0;
    virtual size_t readBytes(uint8_t* buffer, size_t length) = 0;
    virtual int available() = 0;
    virtual int read() = 0;
};

} // namespace hal
