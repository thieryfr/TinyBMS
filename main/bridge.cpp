#include "bridge.hpp"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <strings.h>
#include <cmath>
#include <algorithm>

namespace tinybms {
namespace {
constexpr const char *TAG = "tinybms-bridge";
constexpr size_t UART_LINE_BUFFER = 192;
constexpr TickType_t UART_READ_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t CAN_KEEPALIVE_ID = 0x351;
constexpr uint32_t CAN_STATUS_ID = 0x355;
constexpr uint8_t CAN_FRAME_DLC = 8;

inline uint32_t get_time_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void configure_led(gpio_num_t pin) {
    if (pin == GPIO_NUM_NC) {
        return;
    }
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void set_led(gpio_num_t pin, bool on) {
    if (pin == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(pin, on ? 1 : 0);
}

} // namespace

TinyBmsBridge::TinyBmsBridge(const BridgeConfig &config)
    : config_(config),
      sample_queue_(nullptr),
      uart_task_handle_(nullptr),
      can_task_handle_(nullptr),
      diag_task_handle_(nullptr),
      running_(false),
      latest_lock_(nullptr),
      latest_sample_{},
      has_sample_(false) {}

TinyBmsBridge::~TinyBmsBridge() {
    stop();
}

esp_err_t TinyBmsBridge::init() {
    ESP_LOGI(TAG, "Initialising bridge");
    esp_err_t diag_err = diagnostics::init(health_);
    if (diag_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise diagnostics: %s", esp_err_to_name(diag_err));
        return diag_err;
    }

    latest_lock_ = xSemaphoreCreateMutex();
    if (!latest_lock_) {
        ESP_LOGE(TAG, "Failed to allocate sample mutex");
        diagnostics::destroy(health_);
        return ESP_ERR_NO_MEM;
    }

    sample_queue_ = xQueueCreate(config_.timings.sample_queue_length, sizeof(MeasurementSample));
    if (!sample_queue_) {
        ESP_LOGE(TAG, "Failed to create sample queue");
        vSemaphoreDelete(latest_lock_);
        latest_lock_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    configure_led(config_.pins.status_led);
    set_led(config_.pins.status_led, false);

    uart_config_t uart_cfg{};
    uart_cfg.baud_rate = static_cast<int>(config_.timings.uart_baudrate);
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_APB;

    esp_err_t err = uart_param_config(config_.uart_port, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(config_.uart_port, 2048, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(config_.uart_port, config_.pins.uart_tx, config_.pins.uart_rx,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = twai_driver_install(&config_.can_general, &config_.can_timing, &config_.can_filter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver: %s", esp_err_to_name(err));
        uart_driver_delete(config_.uart_port);
        return err;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        uart_driver_delete(config_.uart_port);
        return err;
    }

    running_.store(true);
    return ESP_OK;
}

esp_err_t TinyBmsBridge::start() {
    if (!running_.load()) {
        ESP_LOGE(TAG, "Bridge not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    if (uart_task_handle_ || can_task_handle_ || diag_task_handle_) {
        ESP_LOGW(TAG, "Tasks already running");
        return ESP_OK;
    }

    BaseType_t created = xTaskCreatePinnedToCore(uart_task_entry, "tinybms_uart", 4096, this, 12, &uart_task_handle_, 0);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART task");
        return ESP_ERR_NO_MEM;
    }

    created = xTaskCreatePinnedToCore(can_task_entry, "tinybms_can", 4096, this, 13, &can_task_handle_, 1);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CAN task");
        vTaskDelete(uart_task_handle_);
        uart_task_handle_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    created = xTaskCreate(diagnostic_task_entry, "tinybms_diag", 3072, this, 5, &diag_task_handle_);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create diagnostic task");
        vTaskDelete(uart_task_handle_);
        vTaskDelete(can_task_handle_);
        uart_task_handle_ = nullptr;
        can_task_handle_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Bridge tasks started");
    return ESP_OK;
}

void TinyBmsBridge::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);

    if (uart_task_handle_) {
        vTaskDelete(uart_task_handle_);
        uart_task_handle_ = nullptr;
    }
    if (can_task_handle_) {
        vTaskDelete(can_task_handle_);
        can_task_handle_ = nullptr;
    }
    if (diag_task_handle_) {
        vTaskDelete(diag_task_handle_);
        diag_task_handle_ = nullptr;
    }

    if (sample_queue_) {
        vQueueDelete(sample_queue_);
        sample_queue_ = nullptr;
    }

    diagnostics::destroy(health_);

    if (latest_lock_) {
        vSemaphoreDelete(latest_lock_);
        latest_lock_ = nullptr;
    }

    twai_stop();
    twai_driver_uninstall();
    uart_driver_delete(config_.uart_port);
}

void TinyBmsBridge::uart_task_entry(void *arg) {
    auto *self = static_cast<TinyBmsBridge *>(arg);
    self->uart_task();
    vTaskDelete(nullptr);
}

void TinyBmsBridge::can_task_entry(void *arg) {
    auto *self = static_cast<TinyBmsBridge *>(arg);
    self->can_task();
    vTaskDelete(nullptr);
}

void TinyBmsBridge::diagnostic_task_entry(void *arg) {
    auto *self = static_cast<TinyBmsBridge *>(arg);
    self->diagnostic_task();
    vTaskDelete(nullptr);
}

bool TinyBmsBridge::parse_sample_line(const char *line, MeasurementSample &out_sample) {
    if (!line || line[0] == '\0') {
        return false;
    }

    float voltage = NAN;
    float current = NAN;
    float soc = NAN;
    float temperature = NAN;

    char buffer[UART_LINE_BUFFER];
    strlcpy(buffer, line, sizeof(buffer));

    char *save = nullptr;
    for (char *token = strtok_r(buffer, ";,", &save); token; token = strtok_r(nullptr, ";,", &save)) {
        char *eq = strchr(token, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *key = token;
        const char *value = eq + 1;
        if (strcasecmp(key, "V") == 0 || strcasecmp(key, "voltage") == 0) {
            voltage = strtof(value, nullptr);
        } else if (strcasecmp(key, "I") == 0 || strcasecmp(key, "current") == 0) {
            current = strtof(value, nullptr);
        } else if (strcasecmp(key, "SOC") == 0) {
            soc = strtof(value, nullptr);
        } else if (strcasecmp(key, "T") == 0 || strcasecmp(key, "temp") == 0) {
            temperature = strtof(value, nullptr);
        }
    }

    if (std::isnan(voltage) || std::isnan(current) || std::isnan(soc)) {
        return false;
    }

    out_sample.timestamp_ms = get_time_ms();
    out_sample.pack_voltage_v = voltage;
    out_sample.pack_current_a = current;
    out_sample.soc_percent = std::clamp(soc, 0.0f, 100.0f);
    out_sample.temperature_c = std::isnan(temperature) ? 25.0f : temperature;
    return true;
}

void TinyBmsBridge::publish_sample(const MeasurementSample &sample) {
    if (!sample_queue_) {
        return;
    }
    if (latest_lock_ && xSemaphoreTake(latest_lock_, pdMS_TO_TICKS(10)) == pdTRUE) {
        latest_sample_ = sample;
        has_sample_ = true;
        xSemaphoreGive(latest_lock_);
    }
    if (xQueueSend(sample_queue_, &sample, pdMS_TO_TICKS(10)) != pdTRUE) {
        diagnostics::note_dropped_sample(health_);
    } else {
        diagnostics::note_parsed_sample(health_);
    }
}

void TinyBmsBridge::uart_task() {
    ESP_LOGI(TAG, "UART task running");
    uint8_t byte = 0;
    char line[UART_LINE_BUFFER];
    size_t pos = 0;

    while (running_.load()) {
        int len = uart_read_bytes(config_.uart_port, &byte, 1, UART_READ_TIMEOUT);
        if (len <= 0) {
            continue;
        }

        diagnostics::note_uart_activity(health_);

        if (byte == '\r') {
            continue;
        }

        if (byte == '\n') {
            line[pos] = '\0';
            if (pos > 0) {
                MeasurementSample sample{};
                if (parse_sample_line(line, sample)) {
                    publish_sample(sample);
                }
            }
            pos = 0;
            continue;
        }

        if (pos + 1 < sizeof(line)) {
            line[pos++] = static_cast<char>(byte);
        } else {
            pos = 0; // overflow, drop current line
            diagnostics::note_dropped_sample(health_);
        }
    }

    ESP_LOGI(TAG, "UART task stopping");
}

void TinyBmsBridge::can_task() {
    ESP_LOGI(TAG, "CAN task running");
    MeasurementSample sample{};
    uint64_t last_keepalive = esp_timer_get_time();

    while (running_.load()) {
        if (xQueueReceive(sample_queue_, &sample, pdMS_TO_TICKS(100)) == pdTRUE) {
            twai_message_t msg{};
            msg.identifier = CAN_STATUS_ID;
            msg.extd = 0;
            msg.data_length_code = CAN_FRAME_DLC;

            uint16_t voltage = static_cast<uint16_t>(std::round(sample.pack_voltage_v * 100.0f));
            int16_t current = static_cast<int16_t>(std::round(sample.pack_current_a * 10.0f));
            uint8_t soc = static_cast<uint8_t>(std::round(sample.soc_percent));
            int16_t temperature = static_cast<int16_t>(std::round(sample.temperature_c * 10.0f));

            msg.data[0] = voltage & 0xFF;
            msg.data[1] = (voltage >> 8) & 0xFF;
            msg.data[2] = static_cast<uint8_t>(current & 0xFF);
            msg.data[3] = static_cast<uint8_t>((current >> 8) & 0xFF);
            msg.data[4] = soc;
            msg.data[5] = static_cast<uint8_t>(temperature & 0xFF);
            msg.data[6] = static_cast<uint8_t>((temperature >> 8) & 0xFF);
            msg.data[7] = 0x00;

            esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
            if (err != ESP_OK) {
                diagnostics::note_can_error(health_, err);
                ESP_LOGW(TAG, "Failed to send CAN status frame: %s", esp_err_to_name(err));
            } else {
                diagnostics::note_can_publish(health_);
                set_led(config_.pins.status_led, true);
            }
        }

        uint64_t now = esp_timer_get_time();
        if ((now - last_keepalive) / 1000ULL >= config_.timings.keepalive_period_ms) {
            twai_message_t keepalive{};
            keepalive.identifier = CAN_KEEPALIVE_ID;
            keepalive.extd = 0;
            keepalive.data_length_code = 2;
            keepalive.data[0] = 0xAA;
            keepalive.data[1] = 0x55;
            esp_err_t err = twai_transmit(&keepalive, pdMS_TO_TICKS(50));
            if (err != ESP_OK) {
                diagnostics::note_can_error(health_, err);
                ESP_LOGW(TAG, "Failed to send CAN keepalive: %s", esp_err_to_name(err));
            } else {
                diagnostics::note_can_publish(health_);
            }
            last_keepalive = now;
            set_led(config_.pins.status_led, false);
        }
    }

    ESP_LOGI(TAG, "CAN task stopping");
}

void TinyBmsBridge::diagnostic_task() {
    const TickType_t delay = pdMS_TO_TICKS(config_.timings.diagnostic_period_ms);
    while (running_.load()) {
        diagnostics::log_snapshot(health_, TAG);
        vTaskDelay(delay);
    }
}

bool TinyBmsBridge::latest_sample(MeasurementSample &out) const {
    if (!latest_lock_) {
        return false;
    }
    bool available = false;
    if (xSemaphoreTake(latest_lock_, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (has_sample_) {
            out = latest_sample_;
            available = true;
        }
        xSemaphoreGive(latest_lock_);
    }
    return available;
}

diagnostics::BridgeHealthSnapshot TinyBmsBridge::health_snapshot() const {
    return diagnostics::snapshot(health_);
}

} // namespace tinybms
