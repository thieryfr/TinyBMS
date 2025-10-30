#pragma once

#include "hal/hal_config.h"
#include "hal/hal_types.h"

namespace hal {

class IHalTimer {
public:
    virtual ~IHalTimer() = default;

    virtual Status start(const TimerConfig& config, TimerCallback callback, TimerContext context) = 0;
    virtual Status stop() = 0;
    virtual bool isActive() const = 0;
};

} // namespace hal
