#include "tiny_read_mapping.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <SPIFFS.h>
#endif

#ifdef ARDUINO
#include "logger.h"
#else
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

std::vector<TinyRegisterMetadata> g_metadata;
std::vector<TinyRegisterRuntimeBinding> g_bindings = {
    {0, 1, 36, TinyRegisterValueType::Float, false, 0.01f, TinyLiveDataField::Voltage, "Battery Pack Voltage", "V", nullptr},
    {1, 1, 38, TinyRegisterValueType::Float, true, 0.1f, TinyLiveDataField::Current, "Battery Pack Current", "A", nullptr},
    {2, 1, 46, TinyRegisterValueType::Uint16, false, 0.1f, TinyLiveDataField::SocPercent, "State Of Charge", "%", nullptr},
    {3, 1, 45, TinyRegisterValueType::Uint16, false, 0.1f, TinyLiveDataField::SohPercent, "State Of Health", "%", nullptr},
    {4, 1, 48, TinyRegisterValueType::Int16, true, 0.1f, TinyLiveDataField::Temperature, "Internal Temperature", "°C", nullptr},
    {5, 1, 40, TinyRegisterValueType::Uint16, false, 1.0f, TinyLiveDataField::MinCellMv, "Min Cell Voltage", "mV", nullptr},
    {6, 1, 41, TinyRegisterValueType::Uint16, false, 1.0f, TinyLiveDataField::MaxCellMv, "Max Cell Voltage", "mV", nullptr},
    {7, 1, 51, TinyRegisterValueType::Uint16, false, 1.0f, TinyLiveDataField::BalancingBits, "Balancing Bits", "-", nullptr},
    {8, 1, 103, TinyRegisterValueType::Uint16, false, 0.1f, TinyLiveDataField::MaxChargeCurrent, "Max Charge Current", "A", nullptr},
    {9, 1, 102, TinyRegisterValueType::Uint16, false, 0.1f, TinyLiveDataField::MaxDischargeCurrent, "Max Discharge Current", "A", nullptr}
};

std::unordered_map<uint16_t, const TinyRegisterMetadata*> g_metadata_lookup;

TinyRegisterValueType parseType(const char* value) {
    if (!value) {
        return TinyRegisterValueType::Unknown;
    }

    std::string type_str(value);
    std::transform(type_str.begin(), type_str.end(), type_str.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    if (type_str.find("FLOAT") != std::string::npos) {
        return TinyRegisterValueType::Float;
    }
    if (type_str.find("INT16") != std::string::npos) {
        return TinyRegisterValueType::Int16;
    }
    if (type_str.find("UINT32") != std::string::npos) {
        return TinyRegisterValueType::Uint32;
    }
    if (type_str.find("UINT16") != std::string::npos) {
        return TinyRegisterValueType::Uint16;
    }
    if (type_str.find("UINT8") != std::string::npos) {
        return TinyRegisterValueType::Uint8;
    }
    if (type_str.find("STRING") != std::string::npos) {
        return TinyRegisterValueType::String;
    }

    return TinyRegisterValueType::Unknown;
}

std::vector<uint16_t> parseAddresses(const char* key) {
    std::vector<uint16_t> result;
    if (!key) {
        return result;
    }

    std::stringstream ss(key);
    std::string token;
    while (std::getline(ss, token, '.')) {
        if (token.empty()) {
            continue;
        }
        try {
            int value = std::stoi(token);
            if (value >= 0 && value <= 0xFFFF) {
                result.push_back(static_cast<uint16_t>(value));
            }
        } catch (...) {
            // Ignore invalid segments
        }
    }

    return result;
}

void rebuildLookup() {
    g_metadata_lookup.clear();
    for (const auto& meta : g_metadata) {
        for (uint16_t addr : meta.addresses) {
            g_metadata_lookup[addr] = &meta;
        }
    }

    for (auto& binding : g_bindings) {
        binding.metadata = nullptr;
        if (binding.metadata_address == 0) {
            continue;
        }

        auto it = g_metadata_lookup.find(binding.metadata_address);
        if (it != g_metadata_lookup.end()) {
            binding.metadata = it->second;
            continue;
        }

        // If not found directly, try to match against primary addresses
        for (const auto& meta : g_metadata) {
            if (meta.primary_address == binding.metadata_address) {
                binding.metadata = &meta;
                break;
            }
        }
    }
}

void addMetadataEntry(const std::string& raw_key,
                      const std::string& name,
                      const std::string& type,
                      const std::string& unit,
                      const std::string& comment) {
    TinyRegisterMetadata meta;
    meta.addresses = parseAddresses(raw_key.c_str());
    if (!meta.addresses.empty()) {
        meta.primary_address = meta.addresses.front();
    }

    meta.raw_key = raw_key.c_str();
    meta.name = name.c_str();
    meta.unit = unit.c_str();
    meta.comment = comment.c_str();
    meta.type = parseType(type.c_str());

    g_metadata.push_back(std::move(meta));
}

#ifdef ARDUINO
bool parseDocument(const JsonVariantConst& root, Logger* logger) {
    if (!root.is<JsonObject>()) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] tiny_read.json root is not an object");
        }
        return false;
    }

    auto registers = root["tiny_read_registers"];
    if (!registers.is<JsonObject>()) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] 'tiny_read_registers' missing or invalid");
        }
        return false;
    }

    for (auto kv : registers.as<JsonObject>()) {
        JsonVariantConst entry = kv.value();
        const char* name = entry["tiny_name"].as<const char*>();
        const char* unit = entry["tiny_scale_unit"].as<const char*>();
        const char* comment = entry["comment"].as<const char*>();
        const char* type = entry["tiny_type"].as<const char*>();
        addMetadataEntry(kv.key().c_str(),
                         name ? std::string(name) : std::string(),
                         type ? std::string(type) : std::string(),
                         unit ? std::string(unit) : std::string(),
                         comment ? std::string(comment) : std::string());
    }

    rebuildLookup();

    if (logger) {
        logger->log(LOG_INFO, "[MAPPING] Loaded " + String(g_metadata.size()) + " tiny_read entries");
    }

    return true;
}
#endif

#ifndef ARDUINO
std::string extractField(const std::string& object, const char* field) {
    std::string pattern = "\"" + std::string(field) + "\"";
    size_t pos = object.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }
    pos = object.find(':', pos);
    if (pos == std::string::npos) {
        return {};
    }
    size_t first = object.find('"', pos);
    if (first == std::string::npos) {
        return {};
    }
    size_t second = object.find('"', first + 1);
    if (second == std::string::npos) {
        return {};
    }
    return object.substr(first + 1, second - first - 1);
}

bool parseJsonFallback(const char* json, Logger* logger) {
    if (!json) {
        return false;
    }

    std::string content(json);
    size_t regs_pos = content.find("\"tiny_read_registers\"");
    if (regs_pos == std::string::npos) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] 'tiny_read_registers' not found in JSON");
        }
        return false;
    }

    regs_pos = content.find('{', regs_pos);
    if (regs_pos == std::string::npos) {
        return false;
    }

    size_t depth = 0;
    size_t end = regs_pos;
    for (; end < content.size(); ++end) {
        if (content[end] == '{') {
            depth++;
        } else if (content[end] == '}') {
            if (depth == 0) {
                break;
            }
            depth--;
            if (depth == 0) {
                ++end;
                break;
            }
        }
    }

    if (depth != 0 || end <= regs_pos) {
        return false;
    }

    std::string block = content.substr(regs_pos, end - regs_pos);
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
        size_t obj_depth = 0;
        size_t obj_end = obj_start;
        for (; obj_end < block.size(); ++obj_end) {
            if (block[obj_end] == '{') {
                obj_depth++;
            } else if (block[obj_end] == '}') {
                if (obj_depth == 0) {
                    break;
                }
                obj_depth--;
                if (obj_depth == 0) {
                    ++obj_end;
                    break;
                }
            }
        }
        if (obj_depth != 0 || obj_end <= obj_start) {
            break;
        }
        std::string object = block.substr(obj_start, obj_end - obj_start);
        std::string name = extractField(object, "tiny_name");
        std::string type = extractField(object, "tiny_type");
        std::string unit = extractField(object, "tiny_scale_unit");
        std::string comment = extractField(object, "comment");
        addMetadataEntry(key, name, type, unit, comment);
        pos = obj_end;
    }

    rebuildLookup();

    if (logger) {
        logger->log(LOG_INFO, "[MAPPING] Loaded " + String(g_metadata.size()) + " tiny_read entries (fallback)");
    }

    return !g_metadata.empty();
}
#endif

} // namespace

bool loadTinyReadMappingFromJson(const char* json, Logger* logger) {
    if (!json) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] JSON buffer is null");
        }
        return false;
    }

    auto previous_metadata = g_metadata;
    g_metadata.clear();
    g_metadata_lookup.clear();

#ifdef ARDUINO
    StaticJsonDocument<8192> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        if (logger) {
            logger->log(LOG_ERROR, "[MAPPING] Failed to parse JSON: " + String(err.c_str()));
        }
        g_metadata = std::move(previous_metadata);
        rebuildLookup();
        return false;
    }

    if (!parseDocument(doc.as<JsonVariantConst>(), logger)) {
        g_metadata = std::move(previous_metadata);
        rebuildLookup();
        return false;
    }
    return true;
#else
    if (!parseJsonFallback(json, logger)) {
        g_metadata = std::move(previous_metadata);
        rebuildLookup();
        return false;
    }
    return true;
#endif
}

bool initializeTinyReadMapping(fs::FS& fs, const char* path, Logger* logger) {
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
    return loadTinyReadMappingFromJson(json.c_str(), logger);
#else
    (void)fs;
    (void)path;
    (void)logger;
    return false;
#endif
}

const std::vector<TinyRegisterMetadata>& getTinyRegisterMetadata() {
    return g_metadata;
}

const TinyRegisterMetadata* findTinyRegisterMetadata(uint16_t address) {
    auto it = g_metadata_lookup.find(address);
    if (it != g_metadata_lookup.end()) {
        return it->second;
    }
    return nullptr;
}

const std::vector<TinyRegisterRuntimeBinding>& getTinyRegisterBindings() {
    return g_bindings;
}

const TinyRegisterRuntimeBinding* findTinyRegisterBinding(uint16_t address) {
    for (const auto& binding : g_bindings) {
        if (binding.metadata_address == address) {
            return &binding;
        }
    }
    return nullptr;
}

String tinyRegisterTypeToString(TinyRegisterValueType type) {
    switch (type) {
        case TinyRegisterValueType::Uint8:
            return "UINT8";
        case TinyRegisterValueType::Uint16:
            return "UINT16";
        case TinyRegisterValueType::Uint32:
            return "UINT32";
        case TinyRegisterValueType::Int16:
            return "INT16";
        case TinyRegisterValueType::Float:
            return "FLOAT";
        case TinyRegisterValueType::String:
            return "STRING";
        case TinyRegisterValueType::Unknown:
        default:
            return "UNKNOWN";
    }
}

