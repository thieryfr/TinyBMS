#pragma once

#include <memory>
#include <mutex>
#include "hal/hal_config.h"
#include "hal/hal_factory.h"

namespace hal {

class HalManager {
public:
    static HalManager& instance();

    void initialize(const HalConfig& config);

    IHalUart& uart();
    IHalCan& can();
    IHalStorage& storage();
    IHalWatchdog& watchdog();

    bool isInitialized() const;

private:
    HalManager() = default;

    HalConfig config_{};
    std::unique_ptr<IHalUart> uart_;
    std::unique_ptr<IHalCan> can_;
    std::unique_ptr<IHalStorage> storage_;
    std::unique_ptr<IHalWatchdog> watchdog_;
    bool initialized_ = false;
};

} // namespace hal
