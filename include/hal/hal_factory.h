#pragma once

#include <memory>
#include "hal/hal_config.h"
#include "hal/interfaces/ihal_can.h"
#include "hal/interfaces/ihal_gpio.h"
#include "hal/interfaces/ihal_storage.h"
#include "hal/interfaces/ihal_timer.h"
#include "hal/interfaces/ihal_uart.h"
#include "hal/interfaces/ihal_watchdog.h"

namespace hal {

class HalFactory {
public:
    virtual ~HalFactory() = default;

    virtual std::unique_ptr<IHalUart> createUart() = 0;
    virtual std::unique_ptr<IHalCan> createCan() = 0;
    virtual std::unique_ptr<IHalStorage> createStorage() = 0;
    virtual std::unique_ptr<IHalGpio> createGpio() = 0;
    virtual std::unique_ptr<IHalTimer> createTimer() = 0;
    virtual std::unique_ptr<IHalWatchdog> createWatchdog() = 0;
};

void setFactory(std::unique_ptr<HalFactory> factory);
HalFactory& factory();

} // namespace hal
