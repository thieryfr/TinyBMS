#include "hal/interfaces/ihal_can.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <driver/twai.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace hal {

namespace {

QueueHandle_t createQueue(size_t length) {
    return xQueueCreate(length, sizeof(CanFrame));
}

SemaphoreHandle_t createMutex() {
    return xSemaphoreCreateMutex();
}

twai_timing_config_t selectTiming(uint32_t bitrate, bool& ok) {
    ok = true;
    switch (bitrate) {
        case 25000:   return TWAI_TIMING_CONFIG_25KBITS();
        case 50000:   return TWAI_TIMING_CONFIG_50KBITS();
        case 100000:  return TWAI_TIMING_CONFIG_100KBITS();
        case 125000:  return TWAI_TIMING_CONFIG_125KBITS();
        case 250000:  return TWAI_TIMING_CONFIG_250KBITS();
        case 500000:  return TWAI_TIMING_CONFIG_500KBITS();
        case 800000:  return TWAI_TIMING_CONFIG_800KBITS();
        case 1000000: return TWAI_TIMING_CONFIG_1MBITS();
        default:
            ok = false;
            return TWAI_TIMING_CONFIG_250KBITS();
    }
}

twai_filter_config_t buildFilter(const std::vector<CanFilterConfig>& filters) {
    if (filters.empty()) {
        return TWAI_FILTER_CONFIG_ACCEPT_ALL();
    }

    // ESP-IDF TWAI driver only supports a single mask-based filter.
    const auto& cfg = filters.front();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    filter.acceptance_code = cfg.id;
    filter.acceptance_mask = cfg.mask;
    filter.single_filter = true;
    return filter;
}

} // namespace

class Esp32Can : public IHalCan {
public:
    Esp32Can() = default;

    ~Esp32Can() override {
        if (initialized_) {
            twai_stop();
            twai_driver_uninstall();
        }
        if (rx_queue_) {
            vQueueDelete(rx_queue_);
        }
        if (mutex_) {
            vSemaphoreDelete(mutex_);
        }
    }

    Status initialize(const CanConfig& config) override {
        config_ = config;

        if (!mutex_) {
            mutex_ = createMutex();
        }
        if (!rx_queue_) {
            rx_queue_ = createQueue(32);
        }
        if (!mutex_ || !rx_queue_) {
            return Status::Error;
        }

        if (initialized_) {
            xQueueReset(rx_queue_);
            stats_ = {};
            return Status::Ok;
        }

        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE) {
            return Status::Busy;
        }

        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
            static_cast<gpio_num_t>(config.tx_pin),
            static_cast<gpio_num_t>(config.rx_pin),
            TWAI_MODE_NORMAL);
        g_config.tx_queue_len = 32;
        g_config.rx_queue_len = 32;
        g_config.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL |
                                  TWAI_ALERT_TX_FAILED | TWAI_ALERT_RECOVERY_COMPLETE |
                                  TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS;

        bool timing_ok = false;
        twai_timing_config_t t_config = selectTiming(config.bitrate, timing_ok);
        if (!timing_ok) {
            xSemaphoreGive(mutex_);
            return Status::InvalidArgument;
        }

        twai_filter_config_t f_config = buildFilter(config.filters);

        esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
        if (err != ESP_OK) {
            xSemaphoreGive(mutex_);
            return Status::Error;
        }

        err = twai_start();
        if (err != ESP_OK) {
            twai_driver_uninstall();
            xSemaphoreGive(mutex_);
            return Status::Error;
        }

        xQueueReset(rx_queue_);
        initialized_ = true;
        recovering_ = false;
        xSemaphoreGive(mutex_);
        return Status::Ok;
    }

    Status transmit(const CanFrame& frame) override {
        if (!initialized_) {
            return Status::Error;
        }

        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
            return Status::Busy;
        }

        pollAlertsLocked();

        twai_message_t message{};
        message.identifier = frame.id;
        message.data_length_code = std::min<uint8_t>(frame.dlc, 8);
        message.flags = 0;
        if (frame.extended) {
            message.flags |= TWAI_MSG_FLAG_EXTD;
        }
        std::memcpy(message.data, frame.data.data(), message.data_length_code);

        esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(20));
        if (err == ESP_ERR_TIMEOUT) {
            stats_.tx_errors++;
            xSemaphoreGive(mutex_);
            return Status::Timeout;
        }
        if (err != ESP_OK) {
            stats_.tx_errors++;
            xSemaphoreGive(mutex_);
            return Status::Error;
        }

        stats_.tx_success++;
        xSemaphoreGive(mutex_);
        return Status::Ok;
    }

    Status receive(CanFrame& frame, uint32_t timeout_ms) override {
        if (!initialized_) {
            return Status::Error;
        }

        pumpRxQueue();

        CanFrame tmp{};
        if (xQueueReceive(rx_queue_, &tmp, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
            frame = tmp;
            stats_.rx_success++;
            return Status::Ok;
        }
        return timeout_ms == 0 ? Status::Timeout : Status::Timeout;
    }

    Status configureFilters(const std::vector<CanFilterConfig>& filters) override {
        if (!initialized_) {
            return Status::Error;
        }
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
            return Status::Busy;
        }
        twai_filter_config_t filter = buildFilter(filters);
        esp_err_t err = twai_set_filter(&filter);
        xSemaphoreGive(mutex_);
        return err == ESP_OK ? Status::Ok : Status::Error;
    }

    CanStats getStats() const override {
        return stats_;
    }

    void resetStats() override {
        stats_ = {};
    }

private:
    void pumpRxQueue() {
        if (!initialized_) {
            return;
        }

        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
            return;
        }

        pollAlertsLocked();

        twai_message_t message{};
        while (true) {
            esp_err_t err = twai_receive(&message, 0);
            if (err == ESP_ERR_TIMEOUT) {
                break;
            }
            if (err != ESP_OK) {
                stats_.rx_errors++;
                break;
            }

            CanFrame frame{};
            frame.id = message.identifier;
            frame.dlc = std::min<uint8_t>(message.data_length_code, 8);
            frame.extended = (message.flags & TWAI_MSG_FLAG_EXTD) != 0;
            std::memcpy(frame.data.data(), message.data, frame.dlc);

            if (xQueueSend(rx_queue_, &frame, 0) != pdTRUE) {
                stats_.rx_dropped++;
                break;
            }
        }

        xSemaphoreGive(mutex_);
    }

    void pollAlertsLocked() {
        if (!initialized_) {
            return;
        }

        uint32_t alerts = 0;
        esp_err_t err = twai_read_alerts(&alerts, 0);
        if (err == ESP_ERR_TIMEOUT) {
            return;
        }
        if (err != ESP_OK) {
            return;
        }

        if (alerts & TWAI_ALERT_BUS_OFF) {
            stats_.bus_off_events++;
            esp_err_t rec = twai_initiate_recovery();
            if (rec == ESP_OK) {
                recovering_ = true;
            }
        }

        if (alerts & TWAI_ALERT_RECOVERY_COMPLETE) {
            esp_err_t start_err = twai_start();
            if (start_err == ESP_OK) {
                recovering_ = false;
            }
        }

        if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
            stats_.rx_dropped++;
        }

        if (alerts & TWAI_ALERT_TX_FAILED) {
            stats_.tx_errors++;
        }

        (void)recovering_;
    }

    SemaphoreHandle_t mutex_ = nullptr;
    QueueHandle_t rx_queue_ = nullptr;
    bool initialized_ = false;
    bool recovering_ = false;
    CanStats stats_{};
    CanConfig config_{};
};

std::unique_ptr<IHalCan> createEsp32Can() {
    return std::make_unique<Esp32Can>();
}

} // namespace hal
