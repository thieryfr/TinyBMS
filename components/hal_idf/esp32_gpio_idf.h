/**
 * @file esp32_gpio_idf.h
 * @brief ESP-IDF native GPIO HAL implementation header
 */

#pragma once

#include "hal/interfaces/ihal_gpio.h"
#include "driver/gpio.h"

namespace hal {

class ESP32GpioIDF : public IHalGpio {
public:
    ESP32GpioIDF();
    ~ESP32GpioIDF() override = default;

    Status configure(const GpioConfig& config) override;
    Status write(GpioLevel level) override;
    GpioLevel read() override;

private:
    int pin_;
    bool configured_;
};

} // namespace hal
