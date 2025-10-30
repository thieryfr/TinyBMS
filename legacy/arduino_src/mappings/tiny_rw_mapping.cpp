#include "tiny_rw_mapping.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <SPIFFS.h>
#endif

#ifdef ARDUINO
#include "logger.h"
#else
#include <sstream>

enum LogLevel {
    LOG_ERROR = 0,
    LOG_WARN = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3
};

class Logger {
public:
    void log(LogLevel, const String&) {}
};
#endif

namespace {

std::vector<TinyRwRegisterMetadata> g_registers;
std::unordered_map<uint16_t, size_t> g_lookup_by_address;
std::unordered_map<std::string, size_t> g_lookup_by_key;

TinyRegisterAccess parseAccess(const char* value) {
    if (!value) {
        return TinyRegisterAccess::ReadWrite;
    }

    String tmp = value;
    tmp.trim();
    tmp.toLowerCase();

    if (tmp == "ro" || tmp == "r") {
        return TinyRegisterAccess::ReadOnly;
    }
    if (tmp == "wo" || tmp == "w") {
        return TinyRegisterAccess::WriteOnly;
    }
    return TinyRegisterAccess::ReadWrite;
}

TinyRegisterValueClass parseValueClass(const char* value, bool has_enum) {
    if (has_enum) {
        return TinyRegisterValueClass::Enum;
    }
    if (!value) {
        return TinyRegisterValueClass::Unknown;
    }

    String tmp = value;
    tmp.trim();
    tmp.toLowerCase();

    if (tmp.indexOf("enum") >= 0) {
        return TinyRegisterValueClass::Enum;
    }
    if (tmp.indexOf("int") >= 0 && tmp.indexOf("uint") < 0) {
        return TinyRegisterValueClass::Int;
    }
    if (tmp.indexOf("float") >= 0) {
        return TinyRegisterValueClass::Float;
    }
    return TinyRegisterValueClass::Uint;
}

void rebuildLookup() {
    g_lookup_by_address.clear();
    g_lookup_by_key.clear();

    for (size_t idx = 0; idx < g_registers.size(); ++idx) {
        const auto& meta = g_registers[idx];
        g_lookup_by_address[meta.address] = idx;
        if (meta.key.length() > 0) {
            g_lookup_by_key[std::string(meta.key.c_str())] = idx;
        }
    }
}

float parseNumber(const JsonVariantConst& value, float fallback = 0.0f) {
    if (value.isNull()) {
        return fallback;
    }
    if (value.is<int>() || value.is<long>() || value.is<long long>()) {
        return static_cast<float>(value.as<long long>());
    }
    if (value.is<unsigned>() || value.is<unsigned long>() || value.is<unsigned long long>()) {
        return static_cast<float>(value.as<unsigned long long>());
    }
    if (value.is<float>() || value.is<double>()) {
        return value.as<float>();
    }
    if (value.is<const char*>()) {
        return atof(value.as<const char*>());
    }
    return fallback;
}

uint16_t encodeRawValue(const TinyRwRegisterMetadata& meta, float raw_value) {
    if (meta.value_class == TinyRegisterValueClass::Int) {
        long signed_raw = std::lround(raw_value);
        if (signed_raw < -32768) signed_raw = -32768;
        if (signed_raw > 32767) signed_raw = 32767;
        return static_cast<uint16_t>(static_cast<int16_t>(signed_raw));
    }

    long candidate = std::lround(raw_value);
    if (candidate < 0) candidate = 0;
    if (candidate > 65535) candidate = 65535;
    return static_cast<uint16_t>(candidate);
}

void populateMetadataCommon(TinyRwRegisterMetadata& meta) {
    if (meta.step <= 0.0f) {
        meta.step = 1.0f;
    }

    if (meta.precision == 0 && meta.scale < 1.0f) {
        int suggested = static_cast<int>(std::ceil(-std::log10(std::max(meta.scale, 0.0001f))));
        meta.precision = static_cast<uint8_t>(std::clamp(suggested, 0, 4));
    }

    meta.step *= meta.scale;
}

#ifdef ARDUINO
bool parseDocument(const JsonVariantConst& root, Logger* logger) {
    if (!root.is<JsonObject>()) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] tiny_rw_bms.json root is not an object");
        }
        return false;
    }

    JsonObjectConst regs = root["tiny_rw_registers"].as<JsonObjectConst>();
    if (regs.isNull()) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] 'tiny_rw_registers' missing in mapping");
        }
        return false;
    }

    g_registers.clear();

    for (JsonPairConst kv : regs) {
        TinyRwRegisterMetadata meta;
        meta.address = static_cast<uint16_t>(atoi(kv.key().c_str()));

        JsonObjectConst entry = kv.value().as<JsonObjectConst>();
        meta.key = entry["key"].as<const char*>();
        meta.label = entry["label"].as<const char*>();
        meta.unit = entry["unit"].as<const char*>();
        meta.type = entry["type"].as<const char*>();
        meta.group = entry["group"].as<const char*>();
        meta.comment = entry["comment"].as<const char*>();
        meta.scale = parseNumber(entry["scale"], 1.0f);
        meta.offset = parseNumber(entry["offset"], 0.0f);
        meta.step = parseNumber(entry["step"], 0.0f);
        meta.precision = entry["precision"].as<uint8_t>();
        meta.access = parseAccess(entry["access"].as<const char*>());

        bool has_enum = entry.containsKey("enum");
        meta.value_class = parseValueClass(meta.type.c_str(), has_enum);

        const bool has_default = entry.containsKey("default");
        const bool has_min = entry.containsKey("min");
        const bool has_max = entry.containsKey("max");
        const float raw_default = parseNumber(entry["default"], 0.0f);
        const float raw_min = parseNumber(entry["min"], 0.0f);
        const float raw_max = parseNumber(entry["max"], 0.0f);

        if (has_enum) {
            JsonArrayConst options = entry["enum"].as<JsonArrayConst>();
            for (JsonVariantConst optVar : options) {
                JsonObjectConst opt = optVar.as<JsonObjectConst>();
                TinyRegisterEnumOption option;
                option.value = static_cast<uint16_t>(parseNumber(opt["value"], 0.0f));
                option.label = opt["label"].as<const char*>();
                meta.enum_values.push_back(option);
            }
            if (has_default) {
                meta.default_raw = static_cast<uint16_t>(std::lround(raw_default));
            } else if (!meta.enum_values.empty()) {
                meta.default_raw = meta.enum_values.front().value;
            }
        } else if (has_default) {
            meta.default_raw = encodeRawValue(meta, raw_default);
        }

        if (has_min) {
            meta.has_min = true;
            meta.min_value = tinyRwConvertRawToUser(meta, encodeRawValue(meta, raw_min));
        }
        if (has_max) {
            meta.has_max = true;
            meta.max_value = tinyRwConvertRawToUser(meta, encodeRawValue(meta, raw_max));
        }

        meta.default_value = tinyRwConvertRawToUser(meta, meta.default_raw);

        populateMetadataCommon(meta);
        g_registers.push_back(std::move(meta));
    }

    rebuildLookup();

    if (logger) {
        logger->log(LOG_INFO, "[MAPPING] Loaded " + String(g_registers.size()) + " tiny_rw entries");
    }

    return !g_registers.empty();
}
#else
std::string trim(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    size_t end = value.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos) {
        return std::string();
    }
    return value.substr(start, end - start + 1);
}

std::string extractValue(const std::string& object, const char* field) {
    std::string pattern = "\"" + std::string(field) + "\"";
    size_t pos = object.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }
    pos = object.find(':', pos);
    if (pos == std::string::npos) {
        return {};
    }
    pos++;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }
    if (pos >= object.size()) {
        return {};
    }
    if (object[pos] == '"') {
        size_t end = pos + 1;
        while (end < object.size() && object[end] != '"') {
            if (object[end] == '\\') {
                ++end;
            }
            ++end;
        }
        if (end < object.size()) {
            return object.substr(pos, end - pos + 1);
        }
        return {};
    }
    if (object[pos] == '[') {
        int depth = 0;
        size_t end = pos;
        for (; end < object.size(); ++end) {
            if (object[end] == '[') {
                ++depth;
            } else if (object[end] == ']') {
                --depth;
                if (depth == 0) {
                    return object.substr(pos, end - pos + 1);
                }
            }
        }
        return {};
    }
    if (object[pos] == '{') {
        int depth = 0;
        size_t end = pos;
        for (; end < object.size(); ++end) {
            if (object[end] == '{') {
                ++depth;
            } else if (object[end] == '}') {
                --depth;
                if (depth == 0) {
                    return object.substr(pos, end - pos + 1);
                }
            }
        }
        return {};
    }
    size_t end = pos;
    while (end < object.size() && object[end] != ',' && object[end] != '}') {
        ++end;
    }
    return object.substr(pos, end - pos);
}

float parseNumber(const std::string& text, float fallback = 0.0f) {
    if (text.empty()) {
        return fallback;
    }
    char* endptr = nullptr;
    float value = std::strtof(text.c_str(), &endptr);
    if (endptr == text.c_str()) {
        return fallback;
    }
    return value;
}

String parseString(const std::string& text) {
    if (text.empty()) {
        return String();
    }
    if (text.front() == '"' && text.back() == '"' && text.size() >= 2) {
        std::string inner = text.substr(1, text.size() - 2);
        return String(inner.c_str());
    }
    return String(text.c_str());
}

void parseEnumArray(const std::string& array_text, TinyRwRegisterMetadata& meta) {
    size_t pos = 0;
    while (true) {
        size_t start = array_text.find('{', pos);
        if (start == std::string::npos) {
            break;
        }
        int depth = 0;
        size_t end = start;
        for (; end < array_text.size(); ++end) {
            if (array_text[end] == '{') {
                ++depth;
            } else if (array_text[end] == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (end >= array_text.size()) {
            break;
        }
        std::string object = array_text.substr(start, end - start + 1);
        TinyRegisterEnumOption option;
        option.value = static_cast<uint16_t>(parseNumber(extractValue(object, "value"), 0.0f));
        option.label = parseString(extractValue(object, "label"));
        meta.enum_values.push_back(option);
        pos = end + 1;
    }
}

bool parseJsonFallback(const char* json, Logger* logger) {
    if (!json) {
        return false;
    }

    std::string content(json);
    size_t regs_pos = content.find("\"tiny_rw_registers\"");
    if (regs_pos == std::string::npos) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] 'tiny_rw_registers' not found in mapping");
        }
        return false;
    }

    regs_pos = content.find('{', regs_pos);
    if (regs_pos == std::string::npos) {
        return false;
    }

    int depth = 0;
    size_t end = regs_pos;
    for (; end < content.size(); ++end) {
        if (content[end] == '{') {
            ++depth;
        } else if (content[end] == '}') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
    }
    if (depth != 0) {
        return false;
    }

    std::string block = content.substr(regs_pos, end - regs_pos + 1);

    size_t pos = 0;
    while (true) {
        size_t key_start = block.find('"', pos);
        if (key_start == std::string::npos) {
            break;
        }
        size_t key_end = block.find('"', key_start + 1);
        if (key_end == std::string::npos) {
            break;
        }
        std::string key = block.substr(key_start + 1, key_end - key_start - 1);
        size_t obj_start = block.find('{', key_end);
        if (obj_start == std::string::npos) {
            break;
        }
        int obj_depth = 0;
        size_t obj_end = obj_start;
        for (; obj_end < block.size(); ++obj_end) {
            if (block[obj_end] == '{') {
                ++obj_depth;
            } else if (block[obj_end] == '}') {
                --obj_depth;
                if (obj_depth == 0) {
                    break;
                }
            }
        }
        if (obj_end >= block.size()) {
            break;
        }
        std::string object = block.substr(obj_start, obj_end - obj_start + 1);

        TinyRwRegisterMetadata meta;
        meta.address = static_cast<uint16_t>(std::stoi(key));
        meta.key = parseString(extractValue(object, "key"));
        meta.label = parseString(extractValue(object, "label"));
        meta.unit = parseString(extractValue(object, "unit"));
        meta.type = parseString(extractValue(object, "type"));
        meta.group = parseString(extractValue(object, "group"));
        meta.comment = parseString(extractValue(object, "comment"));
        meta.scale = parseNumber(extractValue(object, "scale"), 1.0f);
        meta.offset = parseNumber(extractValue(object, "offset"), 0.0f);
        meta.step = parseNumber(extractValue(object, "step"), 0.0f);
        meta.precision = static_cast<uint8_t>(parseNumber(extractValue(object, "precision"), 0.0f));
        meta.access = parseAccess(parseString(extractValue(object, "access")).c_str());

        bool has_enum = !extractValue(object, "enum").empty();
        meta.value_class = parseValueClass(meta.type.c_str(), has_enum);

        std::string default_text = extractValue(object, "default");
        std::string min_text = extractValue(object, "min");
        std::string max_text = extractValue(object, "max");
        bool has_default = !default_text.empty();
        bool has_min = !min_text.empty();
        bool has_max = !max_text.empty();
        float raw_default = parseNumber(default_text, 0.0f);
        float raw_min = parseNumber(min_text, 0.0f);
        float raw_max = parseNumber(max_text, 0.0f);

        if (has_enum) {
            parseEnumArray(extractValue(object, "enum"), meta);
            if (has_default) {
                meta.default_raw = static_cast<uint16_t>(std::lround(raw_default));
            } else if (!meta.enum_values.empty()) {
                meta.default_raw = meta.enum_values.front().value;
            }
        } else if (has_default) {
            meta.default_raw = encodeRawValue(meta, raw_default);
        }

        if (has_min) {
            meta.has_min = true;
            meta.min_value = tinyRwConvertRawToUser(meta, encodeRawValue(meta, raw_min));
        }
        if (has_max) {
            meta.has_max = true;
            meta.max_value = tinyRwConvertRawToUser(meta, encodeRawValue(meta, raw_max));
        }

        meta.default_value = tinyRwConvertRawToUser(meta, meta.default_raw);

        populateMetadataCommon(meta);
        g_registers.push_back(std::move(meta));
        pos = obj_end + 1;
    }

    rebuildLookup();

    if (logger) {
        logger->log(LOG_INFO, "[MAPPING] Loaded " + String(g_registers.size()) + " tiny_rw entries (fallback)");
    }

    return !g_registers.empty();
}
#endif

} // namespace

bool loadTinyRwMappingFromJson(const char* json, Logger* logger) {
    if (!json) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] JSON buffer is null");
        }
        return false;
    }

    auto previous = g_registers;
    auto prev_lookup = g_lookup_by_address;
    auto prev_key_lookup = g_lookup_by_key;

#ifdef ARDUINO
    StaticJsonDocument<12288> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] Failed to parse tiny_rw JSON: " + String(err.c_str()));
        }
        g_registers = std::move(previous);
        g_lookup_by_address = std::move(prev_lookup);
        g_lookup_by_key = std::move(prev_key_lookup);
        return false;
    }

    if (!parseDocument(doc.as<JsonVariantConst>(), logger)) {
        g_registers = std::move(previous);
        g_lookup_by_address = std::move(prev_lookup);
        g_lookup_by_key = std::move(prev_key_lookup);
        return false;
    }
    return true;
#else
    if (!parseJsonFallback(json, logger)) {
        g_registers = std::move(previous);
        g_lookup_by_address = std::move(prev_lookup);
        g_lookup_by_key = std::move(prev_key_lookup);
        return false;
    }
    return true;
#endif
}

bool initializeTinyRwMapping(fs::FS& fs, const char* path, Logger* logger) {
#ifdef ARDUINO
    File file = fs.open(path, "r");
    if (!file) {
        if (logger) {
            logger->log(LOG_WARN, String("[MAPPING] File not found: ") + path);
        }
        return false;
    }

    String json = file.readString();
    file.close();
    return loadTinyRwMappingFromJson(json.c_str(), logger);
#else
    (void)fs;
    (void)path;
    (void)logger;
    return false;
#endif
}

const std::vector<TinyRwRegisterMetadata>& getTinyRwRegisters() {
    return g_registers;
}

const TinyRwRegisterMetadata* findTinyRwRegister(uint16_t address) {
    auto it = g_lookup_by_address.find(address);
    if (it == g_lookup_by_address.end()) {
        return nullptr;
    }
    return &g_registers[it->second];
}

const TinyRwRegisterMetadata* findTinyRwRegisterByKey(const String& key) {
    if (key.isEmpty()) {
        return nullptr;
    }
    auto it = g_lookup_by_key.find(std::string(key.c_str()));
    if (it == g_lookup_by_key.end()) {
        return nullptr;
    }
    return &g_registers[it->second];
}

float tinyRwConvertRawToUser(const TinyRwRegisterMetadata& meta, uint16_t raw_value) {
    float base = static_cast<float>(raw_value);
    if (meta.value_class == TinyRegisterValueClass::Int) {
        base = static_cast<float>(static_cast<int16_t>(raw_value));
    }
    return base * meta.scale + meta.offset;
}

bool tinyRwConvertUserToRaw(const TinyRwRegisterMetadata& meta, float user_value, uint16_t& raw_out) {
    if (meta.value_class == TinyRegisterValueClass::Enum) {
        uint16_t candidate = static_cast<uint16_t>(std::lround(user_value));
        if (!meta.enum_values.empty()) {
            bool found = false;
            for (const auto& option : meta.enum_values) {
                if (option.value == candidate) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        raw_out = candidate;
        return true;
    }

    float denominator = std::abs(meta.scale) < 1e-6f ? 1.0f : meta.scale;
    float scaled = (user_value - meta.offset) / denominator;

    if (meta.value_class == TinyRegisterValueClass::Int) {
        long candidate = std::lround(scaled);
        if (candidate < -32768 || candidate > 32767) {
            return false;
        }
        raw_out = static_cast<uint16_t>(static_cast<int16_t>(candidate));
        return true;
    }

    long candidate = std::lround(scaled);
    if (candidate < 0 || candidate > 65535) {
        return false;
    }
    raw_out = static_cast<uint16_t>(candidate);
    return true;
}

} // namespace
