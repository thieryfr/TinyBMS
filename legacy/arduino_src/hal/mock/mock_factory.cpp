#include "hal/hal_factory.h"

#include <memory>

namespace hal {

std::unique_ptr<IHalUart> createMockUart();
std::unique_ptr<IHalCan> createMockCan();
std::unique_ptr<IHalStorage> createMockStorage();
std::unique_ptr<IHalGpio> createMockGpio();
std::unique_ptr<IHalTimer> createMockTimer();
std::unique_ptr<IHalWatchdog> createMockWatchdog();

class MockHalFactory : public HalFactory {
public:
    std::unique_ptr<IHalUart> createUart() override { return createMockUart(); }
    std::unique_ptr<IHalCan> createCan() override { return createMockCan(); }
    std::unique_ptr<IHalStorage> createStorage() override { return createMockStorage(); }
    std::unique_ptr<IHalGpio> createGpio() override { return createMockGpio(); }
    std::unique_ptr<IHalTimer> createTimer() override { return createMockTimer(); }
    std::unique_ptr<IHalWatchdog> createWatchdog() override { return createMockWatchdog(); }
};

std::unique_ptr<HalFactory> createMockFactory() {
    return std::make_unique<MockHalFactory>();
}

} // namespace hal
