#include <cassert>
#include <cmath>
#include <string>
#include <vector>

#include "mqtt_formatter.hpp"

using tinybms::mqtt::Topics;
using tinybms::mqtt::SampleView;
using tinybms::mqtt::TelemetryPayload;

namespace {

class MockPublisher : public tinybms::mqtt::Publisher {
  public:
    void publish(const std::string &topic,
                 const std::string &payload,
                 int qos,
                 bool retain) override {
        published_topics.push_back(topic);
        published_payloads.push_back(payload);
        published_qos.push_back(qos);
        published_retain.push_back(retain);
    }

    std::vector<std::string> published_topics;
    std::vector<std::string> published_payloads;
    std::vector<int> published_qos;
    std::vector<bool> published_retain;
};

std::string build_reference_payload(const TelemetryPayload &payload) {
    return tinybms::mqtt::payload_to_json(payload);
}

} // namespace

int main() {
    {
        std::string sanitized = tinybms::mqtt::sanitize_topic_path(" Tiny BMS /Main-Array ");
        assert(sanitized == "tiny_bms/main_array");

        sanitized = tinybms::mqtt::sanitize_topic_path("//Victron//GX//");
        assert(sanitized == "victron/gx");

        std::string combined = tinybms::mqtt::build_topic(" TinyBMS / status ", " Alarm Flags ");
        assert(combined == "tinybms/status/alarm_flags");
    }

    {
        SampleView sample{};
        sample.timestamp_ms = 1234;
        sample.pack_voltage_v = 52.10f;
        sample.pack_current_a = -23.45f;
        sample.soc_percent = 87.6f;
        sample.temperature_c = 31.4f;

        TelemetryPayload payload = tinybms::mqtt::build_payload(sample, 7);
        assert(payload.timestamp_ms == 1234);
        assert(payload.sequence == 7);
        assert(std::abs(payload.voltage_v - 52.10f) < 1e-5f);
        assert(payload.voltage_decivolt == 5210);
        assert(std::abs(payload.current_a + 23.45f) < 1e-5f);
        assert(payload.current_deciamp == -235);
        assert(std::abs(payload.soc_percent - 87.6f) < 1e-5f);
        assert(payload.soc_promille == 876);
        assert(std::abs(payload.temperature_c - 31.4f) < 1e-5f);
        assert(payload.temperature_decic == 314);

        std::string json = build_reference_payload(payload);
        assert(json.find("\"voltage_decivolt\":5210") != std::string::npos);
        assert(json.find("\"current_deciamp\":-235") != std::string::npos);
        assert(json.find("\"soc_promille\":876") != std::string::npos);
    }

    {
        Topics topics;
        topics.root = " TinyBMS Root ";
        topics.telemetry = "tinybms / telemetry";
        topics.status = " status";

        SampleView sample{};
        sample.timestamp_ms = 5555;
        sample.pack_voltage_v = 48.5f;
        sample.pack_current_a = 12.4f;
        sample.soc_percent = 101.0f; // should be clamped to 100%
        sample.temperature_c = 24.9f;

        MockPublisher publisher;
        tinybms::mqtt::publish_sample(publisher, topics, sample, 42, 1, true, " live ");

        assert(publisher.published_topics.size() == 1);
        assert(publisher.published_topics[0] == "tinybms/telemetry/live");
        assert(publisher.published_qos[0] == 1);
        assert(publisher.published_retain[0] == true);

        const std::string &payload_json = publisher.published_payloads[0];
        assert(payload_json.find("\"timestamp_ms\":5555") != std::string::npos);
        assert(payload_json.find("\"sequence\":42") != std::string::npos);
        assert(payload_json.find("\"voltage_decivolt\":4850") != std::string::npos);
        assert(payload_json.find("\"current_deciamp\":124") != std::string::npos);
        assert(payload_json.find("\"soc_promille\":1000") != std::string::npos);
        assert(payload_json.find("\"temperature_decic\":249") != std::string::npos);
    }

    return 0;
}

