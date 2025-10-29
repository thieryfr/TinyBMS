#include "hal/hal_factory.h"

#include <memory>

namespace hal {

std::unique_ptr<IHalUart> createEsp32Uart();
std::unique_ptr<IHalCan> createEsp32Can();
std::unique_ptr<IHalStorage> createEsp32Storage();
std::unique_ptr<IHalGpio> createEsp32Gpio();
std::unique_ptr<IHalTimer> createEsp32Timer();
std::unique_ptr<IHalWatchdog> createEsp32Watchdog();

class Esp32HalFactory : public HalFactory {
public:
    std::unique_ptr<IHalUart> createUart() override { return createEsp32Uart(); }
    std::unique_ptr<IHalCan> createCan() override { return createEsp32Can(); }
    std::unique_ptr<IHalStorage> createStorage() override { return createEsp32Storage(); }
    std::unique_ptr<IHalGpio> createGpio() override { return createEsp32Gpio(); }
    std::unique_ptr<IHalTimer> createTimer() override { return createEsp32Timer(); }
    std::unique_ptr<IHalWatchdog> createWatchdog() override { return createEsp32Watchdog(); }
};

std::unique_ptr<HalFactory> createEsp32Factory() {
    return std::make_unique<Esp32HalFactory>();
}

} // namespace hal
