#pragma once

#include "hal/hal_config.h"
#include "hal/hal_types.h"

namespace hal {

class IHalWatchdog {
public:
    virtual ~IHalWatchdog() = default;

    virtual Status configure(const WatchdogConfig& config) = 0;
    virtual Status enable() = 0;
    virtual Status disable() = 0;
    virtual Status feed() = 0;
    virtual WatchdogStats getStats() const = 0;
};

} // namespace hal
