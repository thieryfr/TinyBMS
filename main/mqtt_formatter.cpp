#include "mqtt_formatter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace tinybms::mqtt {
namespace {

std::string sanitize_segment(std::string_view segment) {
    std::string sanitized;
    sanitized.reserve(segment.size());
    bool last_was_separator = false;

    for (char ch : segment) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isalnum(uc)) {
            sanitized.push_back(static_cast<char>(std::tolower(uc)));
            last_was_separator = false;
        } else if (ch == ' ' || ch == '-' || ch == '_' || ch == '.') {
            if (!sanitized.empty() && !last_was_separator) {
                sanitized.push_back('_');
                last_was_separator = true;
            }
        }
    }

    if (!sanitized.empty() && last_was_separator) {
        sanitized.pop_back();
    }

    return sanitized;
}

std::string join_segments(const std::vector<std::string> &segments) {
    std::string topic;
    for (const auto &segment : segments) {
        if (segment.empty()) {
            continue;
        }
        if (!topic.empty()) {
            topic.push_back('/');
        }
        topic += segment;
    }
    return topic;
}

std::string format_float(float value, int precision) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << std::setprecision(std::max(0, precision)) << value;
    return oss.str();
}

std::string sanitize_path_internal(const std::string &raw) {
    std::vector<std::string> segments;
    std::string current;
    current.reserve(raw.size());

    auto flush_segment = [&]() {
        std::string sanitized = sanitize_segment(current);
        segments.push_back(std::move(sanitized));
        current.clear();
    };

    for (char ch : raw) {
        if (ch == '/') {
            flush_segment();
        } else {
            current.push_back(ch);
        }
    }
    flush_segment();

    return join_segments(segments);
}

} // namespace

std::string sanitize_topic_path(const std::string &raw) {
    if (raw.empty()) {
        return {};
    }
    return sanitize_path_internal(raw);
}

std::string build_topic(const std::string &base, const std::string &suffix) {
    std::string sanitized_base = sanitize_topic_path(base);
    std::string sanitized_suffix = sanitize_topic_path(suffix);

    if (sanitized_base.empty()) {
        return sanitized_suffix;
    }
    if (sanitized_suffix.empty()) {
        return sanitized_base;
    }
    return sanitized_base + '/' + sanitized_suffix;
}

TelemetryPayload build_payload(const SampleView &sample, uint32_t sequence) {
    TelemetryPayload payload{};
    payload.timestamp_ms = sample.timestamp_ms;
    payload.sequence = sequence;
    payload.voltage_v = sample.pack_voltage_v;
    payload.voltage_decivolt = static_cast<int32_t>(std::lround(sample.pack_voltage_v * 100.0f));
    payload.current_a = sample.pack_current_a;
    payload.current_deciamp = static_cast<int32_t>(std::lround(sample.pack_current_a * 10.0f));
    float clamped_soc = std::clamp(sample.soc_percent, 0.0f, 100.0f);
    payload.soc_percent = clamped_soc;
    payload.soc_promille = static_cast<int32_t>(std::lround(clamped_soc * 10.0f));
    payload.temperature_c = sample.temperature_c;
    payload.temperature_decic = static_cast<int32_t>(std::lround(sample.temperature_c * 10.0f));
    return payload;
}

std::string payload_to_json(const TelemetryPayload &payload,
                            int voltage_precision,
                            int current_precision,
                            int temperature_precision) {
    std::string json;
    json.reserve(256);

    json += '{';
    json += "\"timestamp_ms\":" + std::to_string(payload.timestamp_ms) + ',';
    json += "\"sequence\":" + std::to_string(payload.sequence) + ',';
    json += "\"voltage_v\":" + format_float(payload.voltage_v, voltage_precision) + ',';
    json += "\"voltage_decivolt\":" + std::to_string(payload.voltage_decivolt) + ',';
    json += "\"current_a\":" + format_float(payload.current_a, current_precision) + ',';
    json += "\"current_deciamp\":" + std::to_string(payload.current_deciamp) + ',';
    json += "\"soc_percent\":" + format_float(payload.soc_percent, 2) + ',';
    json += "\"soc_promille\":" + std::to_string(payload.soc_promille) + ',';
    json += "\"temperature_c\":" + format_float(payload.temperature_c, temperature_precision) + ',';
    json += "\"temperature_decic\":" + std::to_string(payload.temperature_decic);
    json += '}';

    return json;
}

void publish_sample(Publisher &publisher,
                    const Topics &topics,
                    const SampleView &sample,
                    uint32_t sequence,
                    int qos,
                    bool retain,
                    const std::string &suffix) {
    std::string base = topics.telemetry.empty() ? build_topic(topics.root, "telemetry")
                                                : sanitize_topic_path(topics.telemetry);
    std::string topic = suffix.empty() ? base : build_topic(base, suffix);
    TelemetryPayload payload = build_payload(sample, sequence);
    std::string json = payload_to_json(payload);
    publisher.publish(topic, json, qos, retain);
}

} // namespace tinybms::mqtt

