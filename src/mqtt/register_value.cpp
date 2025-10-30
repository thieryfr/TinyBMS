#include "mqtt/publisher.h"

#include <algorithm>
#include <cctype>

namespace mqtt {
namespace {

String sanitizeTopicComponent(const String& candidate, uint16_t fallback_address) {
    if (candidate.length() == 0) {
        return String(fallback_address);
    }

    String sanitized;
    sanitized.reserve(candidate.length());

    for (size_t i = 0; i < candidate.length(); ++i) {
        const char c = candidate.charAt(i);
        if (std::isalnum(static_cast<unsigned char>(c))) {
            sanitized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (c == ' ' || c == '-' || c == '_' || c == '.') {
            if (sanitized.length() > 0 && sanitized.charAt(sanitized.length() - 1) == '_') {
                continue;
            }
            sanitized += '_';
        }
    }

    while (sanitized.length() > 0 && sanitized.charAt(sanitized.length() - 1) == '_') {
        sanitized.remove(sanitized.length() - 1);
    }

    if (sanitized.length() == 0) {
        return String(fallback_address);
    }

    return sanitized;
}

const char* mapDbusPath(const String& suffix) {
    if (suffix.equalsIgnoreCase("battery_pack_voltage")) {
        return "/Dc/0/Voltage";
    }
    if (suffix.equalsIgnoreCase("battery_pack_current")) {
        return "/Dc/0/Current";
    }
    if (suffix.equalsIgnoreCase("internal_temperature")) {
        return "/Dc/0/Temperature";
    }
    if (suffix.equalsIgnoreCase("state_of_charge")) {
        return "/Soc";
    }
    if (suffix.equalsIgnoreCase("state_of_health")) {
        return "/Soh";
    }
    if (suffix.equalsIgnoreCase("max_charge_current")) {
        return "/Info/MaxChargeCurrent";
    }
    if (suffix.equalsIgnoreCase("max_discharge_current")) {
        return "/Info/MaxDischargeCurrent";
    }
    if (suffix.equalsIgnoreCase("overvoltage_cutoff_mv")) {
        return "/Info/BatteryHighVoltage";
    }
    if (suffix.equalsIgnoreCase("undervoltage_cutoff_mv")) {
        return "/Info/BatteryLowVoltage";
    }
    if (suffix.equalsIgnoreCase("pack_power_w")) {
        return "/Dc/0/Power";
    }
    if (suffix.equalsIgnoreCase("system_state")) {
        return "/System/0/State";
    }
    if (suffix.equalsIgnoreCase("alarm_low_voltage")) {
        return "/Alarms/LowVoltage";
    }
    if (suffix.equalsIgnoreCase("alarm_high_voltage")) {
        return "/Alarms/HighVoltage";
    }
    if (suffix.equalsIgnoreCase("alarm_overtemperature")) {
        return "/Alarms/HighTemperature";
    }
    if (suffix.equalsIgnoreCase("alarm_cell_imbalance")) {
        return "/Alarms/CellImbalance";
    }
    if (suffix.equalsIgnoreCase("alarm_communication")) {
        return "/Alarms/Communication";
    }
    if (suffix.equalsIgnoreCase("alarm_system_shutdown")) {
        return "/Alarms/SystemShutdown";
    }
    if (suffix.equalsIgnoreCase("alarm_low_temperature_charge")) {
        return "/Alarms/LowTemperatureCharge";
    }
    return nullptr;
}

void populateMetadataFromReadMapping(const TinyRegisterRuntimeBinding& binding, RegisterValue& out) {
    if (!binding.metadata) {
        return;
    }

    if (out.label.isEmpty() && !binding.metadata->name.isEmpty()) {
        out.label = binding.metadata->name;
    }
    if (out.unit.isEmpty() && !binding.metadata->unit.isEmpty()) {
        out.unit = binding.metadata->unit;
    }
    if (out.key.isEmpty() && !binding.metadata->raw_key.isEmpty()) {
        out.key = binding.metadata->raw_key;
    }
    if (out.comment.isEmpty() && !binding.metadata->comment.isEmpty()) {
        out.comment = binding.metadata->comment;
    }
}

void populateMetadataFromRwMapping(uint16_t address, RegisterValue& out) {
    const TinyRwRegisterMetadata* rw_meta = findTinyRwRegister(address);
    if (!rw_meta) {
        return;
    }

    if (out.key.isEmpty() && !rw_meta->key.isEmpty()) {
        out.key = rw_meta->key;
    }
    if (out.label.isEmpty() && !rw_meta->label.isEmpty()) {
        out.label = rw_meta->label;
    }
    if (out.unit.isEmpty() && !rw_meta->unit.isEmpty()) {
        out.unit = rw_meta->unit;
    }
    if (!rw_meta->comment.isEmpty()) {
        out.comment = rw_meta->comment;
    }
    out.value_class = rw_meta->value_class;
    out.scale = rw_meta->scale;
    out.offset = rw_meta->offset;
    out.precision = rw_meta->precision;
    out.default_value = rw_meta->default_value;
}

} // namespace

bool buildRegisterValue(const TinyRegisterRuntimeBinding& binding,
                        int32_t raw_value,
                        float scaled_value,
                        const String* text_value,
                        const uint16_t* raw_words,
                        uint32_t timestamp_ms,
                        RegisterValue& out) {
    out = RegisterValue{};
    out.address = (binding.metadata_address != 0) ? binding.metadata_address : binding.register_address;
    out.wire_type = binding.value_type;
    out.raw_value = raw_value;
    out.raw_word_count = binding.register_count;
    out.has_numeric_value = (binding.value_type != TinyRegisterValueType::String);
    out.numeric_value = out.has_numeric_value ? scaled_value : 0.0f;
    out.timestamp_ms = timestamp_ms;
    out.scale = binding.scale;

    const uint8_t copy_count = std::min<uint8_t>(binding.register_count, static_cast<uint8_t>(TINY_REGISTER_MAX_WORDS));
    if (raw_words) {
        for (uint8_t i = 0; i < copy_count; ++i) {
            out.raw_words[i] = raw_words[i];
        }
    }
    for (uint8_t i = copy_count; i < TINY_REGISTER_MAX_WORDS; ++i) {
        out.raw_words[i] = 0;
    }

    out.has_text_value = (text_value != nullptr && text_value->length() > 0);
    if (out.has_text_value) {
        out.text_value = *text_value;
    }

    populateMetadataFromReadMapping(binding, out);
    populateMetadataFromRwMapping(out.address, out);

    if (out.topic_suffix.isEmpty()) {
        String candidate = out.key;
        if (candidate.isEmpty()) {
            candidate = out.label;
        }
        if (candidate.isEmpty()) {
            candidate = String(out.address);
        }
        out.topic_suffix = sanitizeTopicComponent(candidate, out.address);
    }

    if (const char* dbus = mapDbusPath(out.topic_suffix)) {
        out.dbus_path = dbus;
    }

    return true;
}

} // namespace mqtt

