#include "hal/interfaces/ihal_gpio.h"

#include <Arduino.h>
#include <memory>

namespace hal {

namespace {

class Esp32Gpio : public IHalGpio {
public:
    Status configure(const GpioConfig& config) override {
        config_ = config;
        uint8_t mode = INPUT;
        switch (config.mode) {
            case GpioMode::Input: mode = INPUT; break;
            case GpioMode::Output: mode = OUTPUT; break;
            case GpioMode::InputPullUp: mode = INPUT_PULLUP; break;
            case GpioMode::InputPullDown: mode = INPUT_PULLDOWN; break;
            case GpioMode::OpenDrain: mode = OUTPUT_OPEN_DRAIN; break;
        }
        pinMode(config.pin, mode);
        if (config.mode == GpioMode::Output || config.mode == GpioMode::OpenDrain) {
            digitalWrite(config.pin, config.initial_level == GpioLevel::High ? HIGH : LOW);
        }
        configured_ = true;
        return Status::Ok;
    }

    Status write(GpioLevel level) override {
        if (!configured_) {
            return Status::Error;
        }
        digitalWrite(config_.pin, level == GpioLevel::High ? HIGH : LOW);
        return Status::Ok;
    }

    GpioLevel read() override {
        if (!configured_) {
            return GpioLevel::Low;
        }
        return digitalRead(config_.pin) ? GpioLevel::High : GpioLevel::Low;
    }

private:
    GpioConfig config_{};
    bool configured_ = false;
};

} // namespace

std::unique_ptr<IHalGpio> createEsp32Gpio() {
    return std::make_unique<Esp32Gpio>();
}

} // namespace hal
