#include "hal/hal_factory.h"

#include <memory>

namespace hal {

namespace {
std::unique_ptr<HalFactory> g_factory;

class NullFactory : public HalFactory {
public:
    std::unique_ptr<IHalUart> createUart() override { return nullptr; }
    std::unique_ptr<IHalCan> createCan() override { return nullptr; }
    std::unique_ptr<IHalStorage> createStorage() override { return nullptr; }
    std::unique_ptr<IHalGpio> createGpio() override { return nullptr; }
    std::unique_ptr<IHalTimer> createTimer() override { return nullptr; }
    std::unique_ptr<IHalWatchdog> createWatchdog() override { return nullptr; }
};

HalFactory& ensureFactory() {
    if (!g_factory) {
        g_factory = std::make_unique<NullFactory>();
    }
    return *g_factory;
}

} // namespace

void setFactory(std::unique_ptr<HalFactory> factory) {
    g_factory = std::move(factory);
    if (!g_factory) {
        g_factory = std::make_unique<NullFactory>();
    }
}

HalFactory& factory() {
    return ensureFactory();
}

} // namespace hal
