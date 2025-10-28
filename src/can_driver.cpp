#include "can_driver.h"

#include <algorithm>
#include <string.h>

#include <driver/twai.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "logger.h"

extern Logger logger;

namespace {

SemaphoreHandle_t s_mutex = nullptr;
QueueHandle_t s_rx_queue = nullptr;
bool s_initialized = false;
bool s_recovering = false;
CanDriverStats s_stats{};

constexpr size_t CAN_RX_QUEUE_LENGTH = 32;

bool ensureMutex() {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
    return s_mutex != nullptr;
}

bool ensureQueue() {
    if (s_rx_queue == nullptr) {
        s_rx_queue = xQueueCreate(CAN_RX_QUEUE_LENGTH, sizeof(CanFrame));
    }
    return s_rx_queue != nullptr;
}

bool selectTimingConfig(uint32_t bitrate, twai_timing_config_t& config_out) {
    switch (bitrate) {
        case 25000:   config_out = TWAI_TIMING_CONFIG_25KBITS(); break;
        case 50000:   config_out = TWAI_TIMING_CONFIG_50KBITS(); break;
        case 100000:  config_out = TWAI_TIMING_CONFIG_100KBITS(); break;
        case 125000:  config_out = TWAI_TIMING_CONFIG_125KBITS(); break;
        case 250000:  config_out = TWAI_TIMING_CONFIG_250KBITS(); break;
        case 500000:  config_out = TWAI_TIMING_CONFIG_500KBITS(); break;
        case 800000:  config_out = TWAI_TIMING_CONFIG_800KBITS(); break;
        case 1000000: config_out = TWAI_TIMING_CONFIG_1MBITS(); break;
        default:
            return false;
    }
    return true;
}

void pollAlertsLocked() {
    if (!s_initialized) {
        return;
    }

    uint32_t alerts = 0;
    esp_err_t err = twai_read_alerts(&alerts, 0);
    if (err == ESP_ERR_TIMEOUT) {
        return;
    }
    if (err != ESP_OK) {
        logger.log(LOG_ERROR, String("[CAN] Failed to read alerts: ") + esp_err_to_name(err));
        return;
    }

    if (alerts & TWAI_ALERT_BUS_OFF) {
        s_stats.bus_off_events++;
        logger.log(LOG_ERROR, "[CAN] Bus-off detected, initiating recovery");
        esp_err_t rec = twai_initiate_recovery();
        if (rec == ESP_OK) {
            s_recovering = true;
        } else if (rec != ESP_ERR_INVALID_STATE) {
            logger.log(LOG_ERROR, String("[CAN] Failed to initiate recovery: ") + esp_err_to_name(rec));
        }
    }

    if (alerts & TWAI_ALERT_RECOVERY_COMPLETE) {
        logger.log(LOG_WARNING, "[CAN] Bus recovery complete, restarting TWAI");
        esp_err_t start_err = twai_start();
        if (start_err == ESP_OK) {
            s_recovering = false;
        } else if (start_err != ESP_ERR_INVALID_STATE) {
            logger.log(LOG_ERROR, String("[CAN] Failed to restart TWAI after recovery: ") + esp_err_to_name(start_err));
        }
    }

    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
        s_stats.rx_dropped++;
        logger.log(LOG_WARNING, "[CAN] Driver RX queue full, dropping frame");
    }

    if (alerts & TWAI_ALERT_TX_FAILED) {
        s_stats.tx_errors++;
        logger.log(LOG_WARNING, "[CAN] Hardware reported TX failure");
    }

    if (alerts & TWAI_ALERT_ERR_PASS) {
        logger.log(LOG_WARNING, "[CAN] Controller entered error passive state");
    }
}

void pumpRxQueueLocked() {
    if (!s_initialized || s_rx_queue == nullptr) {
        return;
    }

    twai_message_t message;
    while (true) {
        esp_err_t err = twai_receive(&message, 0);
        if (err == ESP_ERR_TIMEOUT) {
            break;
        }
        if (err != ESP_OK) {
            s_stats.rx_errors++;
            logger.log(LOG_ERROR, String("[CAN] Failed to read frame: ") + esp_err_to_name(err));
            break;
        }
        CanFrame frame{};
        frame.id = message.identifier;
        frame.dlc = std::min<uint8_t>(message.data_length_code, 8);
        frame.extended = (message.flags & TWAI_MSG_FLAG_EXTD) != 0;
        memcpy(frame.data, message.data, frame.dlc);

        if (xQueueSend(s_rx_queue, &frame, 0) == pdTRUE) {
            s_stats.rx_success++;
        } else {
            s_stats.rx_dropped++;
            logger.log(LOG_WARNING, "[CAN] Application RX queue full, dropping frame");
            break;
        }
    }
}

} // namespace

bool CanDriver::begin(int tx_pin, int rx_pin, uint32_t bitrate) {
    if (!ensureMutex()) {
        logger.log(LOG_ERROR, "[CAN] Failed to create mutex");
        return false;
    }

    if (!ensureQueue()) {
        logger.log(LOG_ERROR, "[CAN] Failed to create RX queue");
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        logger.log(LOG_ERROR, "[CAN] Mutex unavailable during init");
        return false;
    }

    if (s_initialized) {
        xQueueReset(s_rx_queue);
        xSemaphoreGive(s_mutex);
        return true;
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(tx_pin),
                                                                 static_cast<gpio_num_t>(rx_pin),
                                                                 TWAI_MODE_NORMAL);
    g_config.tx_queue_len = 32;
    g_config.rx_queue_len = 32;
    g_config.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL |
                              TWAI_ALERT_TX_FAILED | TWAI_ALERT_RECOVERY_COMPLETE |
                              TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS;

    twai_timing_config_t t_config;
    if (!selectTimingConfig(bitrate, t_config)) {
        logger.log(LOG_ERROR, String("[CAN] Unsupported bitrate: ") + bitrate);
        xSemaphoreGive(s_mutex);
        return false;
    }

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        logger.log(LOG_ERROR, String("[CAN] Driver install failed: ") + esp_err_to_name(err));
        s_stats.tx_errors++;
        xSemaphoreGive(s_mutex);
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        logger.log(LOG_ERROR, String("[CAN] Failed to start TWAI: ") + esp_err_to_name(err));
        twai_driver_uninstall();
        s_stats.tx_errors++;
        xSemaphoreGive(s_mutex);
        return false;
    }

    xQueueReset(s_rx_queue);
    s_initialized = true;
    s_recovering = false;
    s_stats = CanDriverStats{};

    logger.log(LOG_INFO, String("[CAN] TWAI initialized (bitrate=") + bitrate + ", TX pin=" + tx_pin +
                               ", RX pin=" + rx_pin + ")");

    xSemaphoreGive(s_mutex);
    return true;
}

bool CanDriver::send(const CanFrame& frame) {
    if (!s_initialized || !ensureMutex()) {
        return false;
    }

    if (frame.dlc > 8) {
        logger.log(LOG_ERROR, "[CAN] Invalid DLC > 8");
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        logger.log(LOG_ERROR, "[CAN] Mutex unavailable for TX");
        return false;
    }

    pollAlertsLocked();

    twai_message_t message = {};
    message.identifier = frame.id;
    message.data_length_code = frame.dlc;
    if (frame.extended) {
        message.flags |= TWAI_MSG_FLAG_EXTD;
    }
    memcpy(message.data, frame.data, frame.dlc);

    esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(20));
    if (err == ESP_OK) {
        s_stats.tx_success++;
        xSemaphoreGive(s_mutex);
        return true;
    }

    s_stats.tx_errors++;
    logger.log(LOG_WARNING, String("[CAN] TX failed: ") + esp_err_to_name(err));

    pollAlertsLocked();
    xSemaphoreGive(s_mutex);
    return false;
}

bool CanDriver::receive(CanFrame& frame) {
    if (!s_initialized || !ensureMutex() || s_rx_queue == nullptr) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        logger.log(LOG_ERROR, "[CAN] Mutex unavailable for RX");
        return false;
    }

    pollAlertsLocked();
    pumpRxQueueLocked();

    bool has_frame = (xQueueReceive(s_rx_queue, &frame, 0) == pdTRUE);
    if (!has_frame) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    xSemaphoreGive(s_mutex);
    return true;
}

CanDriverStats CanDriver::getStats() {
    if (!ensureMutex()) {
        return s_stats;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        CanDriverStats copy = s_stats;
        xSemaphoreGive(s_mutex);
        return copy;
    }

    return s_stats;
}

void CanDriver::resetStats() {
    if (!ensureMutex()) {
        return;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_stats = CanDriverStats{};
        if (s_rx_queue != nullptr) {
            xQueueReset(s_rx_queue);
        }
        xSemaphoreGive(s_mutex);
    }
}
