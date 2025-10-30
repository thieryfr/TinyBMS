#include "uart/tinybms_decoder.h"

#include <algorithm>
#include <array>
#include <cstring>

using tinybms::events::MqttRegisterEvent;

namespace tinybms::uart::detail {
namespace {

constexpr uint8_t kMaxCopyWords = static_cast<uint8_t>(TINY_REGISTER_MAX_WORDS);

bool collectWords(const TinyRegisterRuntimeBinding& binding,
                  const std::map<uint16_t, uint16_t>& register_values,
                  std::array<uint16_t, TINY_REGISTER_MAX_WORDS>& out,
                  uint8_t& word_count) {
    if (binding.register_count == 0) {
        word_count = 0;
        return false;
    }

    word_count = std::min<uint8_t>(binding.register_count, kMaxCopyWords);
    for (uint8_t idx = 0; idx < binding.register_count; ++idx) {
        const uint16_t address = static_cast<uint16_t>(binding.register_address + idx);
        auto it = register_values.find(address);
        if (it == register_values.end()) {
            return false;
        }
        if (idx < word_count) {
            out[idx] = it->second;
        }
    }

    for (uint8_t idx = word_count; idx < kMaxCopyWords; ++idx) {
        out[idx] = 0;
    }

    return true;
}

int32_t computeRawValue(const TinyRegisterRuntimeBinding& binding,
                        const std::array<uint16_t, TINY_REGISTER_MAX_WORDS>& words,
                        uint8_t word_count) {
    if (binding.value_type == TinyRegisterValueType::String) {
        return 0;
    }

    if (binding.value_type == TinyRegisterValueType::Uint32 && word_count >= 2) {
        const uint32_t low_word = static_cast<uint32_t>(words[0]);
        const uint32_t high_word = static_cast<uint32_t>(words[1]);
        return static_cast<int32_t>((high_word << 16) | low_word);
    }

    if (binding.data_slice == TinyRegisterDataSlice::LowByte ||
        binding.data_slice == TinyRegisterDataSlice::HighByte) {
        const uint8_t byte_value = (binding.data_slice == TinyRegisterDataSlice::LowByte)
            ? static_cast<uint8_t>(words[0] & 0x00FFu)
            : static_cast<uint8_t>((words[0] >> 8) & 0x00FFu);
        if (binding.is_signed) {
            return static_cast<int32_t>(static_cast<int8_t>(byte_value));
        }
        return static_cast<int32_t>(byte_value);
    }

    if (binding.is_signed) {
        return static_cast<int32_t>(static_cast<int16_t>(words[0]));
    }

    return static_cast<int32_t>(words[0]);
}

String buildTextValue(const TinyRegisterRuntimeBinding& binding,
                      const std::array<uint16_t, TINY_REGISTER_MAX_WORDS>& words,
                      uint8_t word_count) {
    String text_value;

    if (binding.value_type == TinyRegisterValueType::String) {
        text_value.reserve(static_cast<size_t>(word_count) * 2U);
        for (uint8_t idx = 0; idx < word_count; ++idx) {
            const char high = static_cast<char>((words[idx] >> 8) & 0xFF);
            const char low = static_cast<char>(words[idx] & 0xFF);
            if (high != '\0') {
                text_value += high;
            }
            if (low != '\0') {
                text_value += low;
            }
        }
    } else if (binding.metadata_address == 501 && word_count >= 2) {
        const uint16_t major = words[0];
        const uint16_t minor = words[1];
        text_value = String(major) + "." + String(minor);
    }

    return text_value;
}

void populateMqttEvent(const TinyRegisterRuntimeBinding& binding,
                       int32_t raw_value,
                       const std::array<uint16_t, TINY_REGISTER_MAX_WORDS>& words,
                       uint8_t word_count,
                       uint32_t timestamp_ms,
                       const String* text_value,
                       MqttRegisterEvent& out) {
    out = MqttRegisterEvent{};
    out.address = (binding.metadata_address != 0) ? binding.metadata_address : binding.register_address;
    out.value_type = binding.value_type;
    out.raw_value = raw_value;
    out.timestamp_ms = timestamp_ms;
    out.raw_word_count = std::min<uint8_t>(word_count, kMaxCopyWords);

    for (uint8_t i = 0; i < out.raw_word_count; ++i) {
        out.raw_words[i] = words[i];
    }
    for (uint8_t i = out.raw_word_count; i < kMaxCopyWords; ++i) {
        out.raw_words[i] = 0;
    }

    out.has_text = (text_value != nullptr && text_value->length() > 0);
    if (out.has_text) {
        size_t copy_len = static_cast<size_t>(text_value->length());
        if (copy_len >= sizeof(out.text_value)) {
            copy_len = sizeof(out.text_value) - 1;
        }
        std::strncpy(out.text_value, text_value->c_str(), copy_len);
        out.text_value[copy_len] = '\0';
    } else {
        out.text_value[0] = '\0';
    }
}

} // namespace

bool decodeAndApplyBinding(const TinyRegisterRuntimeBinding& binding,
                           const std::map<uint16_t, uint16_t>& register_values,
                           TinyBMS_LiveData& live_data,
                           uint32_t timestamp_ms,
                           MqttRegisterEvent* mqtt_event_out) {
    std::array<uint16_t, TINY_REGISTER_MAX_WORDS> raw_words{};
    uint8_t word_count = 0;

    if (!collectWords(binding, register_values, raw_words, word_count)) {
        return false;
    }

    const int32_t raw_value = computeRawValue(binding, raw_words, word_count);
    const float scaled_value = static_cast<float>(raw_value) * binding.scale;
    String text_value = buildTextValue(binding, raw_words, word_count);
    const String* text_ptr = (text_value.length() > 0) ? &text_value : nullptr;

    live_data.applyBinding(binding, raw_value, scaled_value, text_ptr, raw_words.data());

    if (mqtt_event_out != nullptr) {
        populateMqttEvent(binding, raw_value, raw_words, word_count, timestamp_ms, text_ptr, *mqtt_event_out);
    }

    return true;
}

void finalizeLiveDataFromRegisters(TinyBMS_LiveData& live_data) {
    if (live_data.max_cell_mv > live_data.min_cell_mv) {
        live_data.cell_imbalance_mv = static_cast<uint16_t>(live_data.max_cell_mv - live_data.min_cell_mv);
    } else {
        live_data.cell_imbalance_mv = 0;
    }

    if (live_data.online_status == 0) {
        live_data.online_status = 0x91;
    }
}

} // namespace tinybms::uart::detail

