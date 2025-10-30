#pragma once

#include "config.hpp"
#include "diagnostics.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <atomic>

namespace tinybms {

struct MeasurementSample {
    uint32_t timestamp_ms;
    float pack_voltage_v;
    float pack_current_a;
    float soc_percent;
    float temperature_c;
};

class TinyBmsBridge {
  public:
    explicit TinyBmsBridge(const BridgeConfig &config);
    ~TinyBmsBridge();

    esp_err_t init();
    esp_err_t start();
    void stop();

    bool latest_sample(MeasurementSample &out) const;
    diagnostics::BridgeHealthSnapshot health_snapshot() const;

  private:
    static void uart_task_entry(void *arg);
    static void can_task_entry(void *arg);
    static void diagnostic_task_entry(void *arg);

    void uart_task();
    void can_task();
    void diagnostic_task();

    bool parse_sample_line(const char *line, MeasurementSample &out_sample);
    void publish_sample(const MeasurementSample &sample);

    BridgeConfig config_;
    diagnostics::BridgeHealth health_;
    QueueHandle_t sample_queue_;
    TaskHandle_t uart_task_handle_;
    TaskHandle_t can_task_handle_;
    TaskHandle_t diag_task_handle_;
    std::atomic<bool> running_;
    mutable SemaphoreHandle_t latest_lock_;
    MeasurementSample latest_sample_;
    bool has_sample_;
};

} // namespace tinybms
