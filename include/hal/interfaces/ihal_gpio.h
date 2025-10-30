#pragma once

#include "hal/hal_config.h"
#include "hal/hal_types.h"

namespace hal {

class IHalGpio {
public:
    virtual ~IHalGpio() = default;

    virtual Status configure(const GpioConfig& config) = 0;
    virtual Status write(GpioLevel level) = 0;
    virtual GpioLevel read() = 0;
};

} // namespace hal
