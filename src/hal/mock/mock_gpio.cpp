#include "hal/interfaces/ihal_gpio.h"

#include <memory>

namespace hal {

class MockGpio : public IHalGpio {
public:
    Status configure(const GpioConfig& config) override {
        config_ = config;
        level_ = config.initial_level;
        configured_ = true;
        return Status::Ok;
    }

    Status write(GpioLevel level) override {
        if (!configured_) {
            return Status::Error;
        }
        level_ = level;
        return Status::Ok;
    }

    GpioLevel read() override {
        return level_;
    }

private:
    GpioConfig config_{};
    bool configured_ = false;
    GpioLevel level_ = GpioLevel::Low;
};

std::unique_ptr<IHalGpio> createMockGpio() {
    return std::make_unique<MockGpio>();
}

} // namespace hal
