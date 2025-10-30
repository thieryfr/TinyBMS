#pragma once

#include <cstdint>
#include <string>

namespace tinybms::mqtt {

struct Topics {
    std::string root;
    std::string telemetry;
    std::string status;
};

struct SampleView {
    uint32_t timestamp_ms;
    float pack_voltage_v;
    float pack_current_a;
    float soc_percent;
    float temperature_c;
};

struct TelemetryPayload {
    uint32_t timestamp_ms = 0;
    uint32_t sequence = 0;
    float voltage_v = 0.0f;
    int32_t voltage_decivolt = 0;
    float current_a = 0.0f;
    int32_t current_deciamp = 0;
    float soc_percent = 0.0f;
    int32_t soc_promille = 0;
    float temperature_c = 0.0f;
    int32_t temperature_decic = 0;
};

class Publisher {
  public:
    virtual ~Publisher() = default;
    virtual void publish(const std::string &topic,
                         const std::string &payload,
                         int qos,
                         bool retain) = 0;
};

std::string sanitize_topic_path(const std::string &raw);

std::string build_topic(const std::string &base, const std::string &suffix);

TelemetryPayload build_payload(const SampleView &sample, uint32_t sequence = 0);

std::string payload_to_json(const TelemetryPayload &payload,
                            int voltage_precision = 3,
                            int current_precision = 3,
                            int temperature_precision = 2);

void publish_sample(Publisher &publisher,
                    const Topics &topics,
                    const SampleView &sample,
                    uint32_t sequence,
                    int qos,
                    bool retain,
                    const std::string &suffix = "live");

} // namespace tinybms::mqtt

