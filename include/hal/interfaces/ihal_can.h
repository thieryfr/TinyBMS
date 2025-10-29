#pragma once

#include <cstdint>
#include <vector>
#include "hal/hal_config.h"
#include "hal/hal_types.h"

namespace hal {

class IHalCan {
public:
    virtual ~IHalCan() = default;

    virtual Status initialize(const CanConfig& config) = 0;
    virtual Status transmit(const CanFrame& frame) = 0;
    virtual Status receive(CanFrame& frame, uint32_t timeout_ms) = 0;
    virtual Status configureFilters(const std::vector<CanFilterConfig>& filters) = 0;
    virtual CanStats getStats() const = 0;
    virtual void resetStats() = 0;
};

} // namespace hal
