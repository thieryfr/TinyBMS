/**
 * @file esp32_gpio_idf.cpp
 * @brief ESP-IDF native GPIO HAL implementation
 */

#include "esp32_gpio_idf.h"
#include "esp_log.h"

namespace hal {

static const char* TAG = "ESP32GpioIDF";

ESP32GpioIDF::ESP32GpioIDF()
    : pin_(-1)
    , configured_(false) {
}

Status ESP32GpioIDF::configure(const GpioConfig& config) {
    if (config.pin < 0 || config.pin > 39) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", config.pin);
        return Status::InvalidArgument;
    }

    pin_ = config.pin;

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pin_);

    // Configure mode
    switch (config.mode) {
        case GpioMode::Input:
            io_conf.mode = GPIO_MODE_INPUT;
            break;
        case GpioMode::Output:
            io_conf.mode = GPIO_MODE_OUTPUT;
            break;
        case GpioMode::InputPullUp:
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            break;
        case GpioMode::InputPullDown:
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        case GpioMode::OpenDrain:
            io_conf.mode = GPIO_MODE_OUTPUT_OD;
            break;
        default:
            return Status::InvalidArgument;
    }

    // Configure pull
    if (config.pull == GpioPull::Up && config.mode == GpioMode::Input) {
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    } else if (config.pull == GpioPull::Down && config.mode == GpioMode::Input) {
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    }

    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return Status::Error;
    }

    // Set initial level for outputs
    if (config.mode == GpioMode::Output || config.mode == GpioMode::OpenDrain) {
        gpio_set_level(static_cast<gpio_num_t>(pin_),
                      config.initial_level == GpioLevel::High ? 1 : 0);
    }

    configured_ = true;
    ESP_LOGI(TAG, "GPIO%d configured", pin_);

    return Status::Ok;
}

Status ESP32GpioIDF::write(GpioLevel level) {
    if (!configured_) {
        return Status::Error;
    }

    esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(pin_),
                                   level == GpioLevel::High ? 1 : 0);
    return (err == ESP_OK) ? Status::Ok : Status::Error;
}

GpioLevel ESP32GpioIDF::read() {
    if (!configured_) {
        return GpioLevel::Low;
    }

    int level = gpio_get_level(static_cast<gpio_num_t>(pin_));
    return (level == 1) ? GpioLevel::High : GpioLevel::Low;
}

} // namespace hal
