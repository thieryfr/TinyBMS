#include "victron_can_mapping.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <SPIFFS.h>
#endif

#include "logger.h"

namespace {

std::vector<VictronPgnDefinition> g_pgn_definitions;

uint16_t parsePgnId(const char* value, bool& ok) {
    ok = false;
    if (!value) {
        return 0;
    }
    while (std::isspace(static_cast<unsigned char>(*value))) {
        ++value;
    }
    int base = 10;
    if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        base = 16;
        value += 2;
    }
    char* endptr = nullptr;
    long parsed = std::strtol(value, &endptr, base);
    if (endptr == value || parsed < 0 || parsed > 0x1FFFFFFF) {
        return 0;
    }
    ok = true;
    return static_cast<uint16_t>(parsed & 0x1FFFFFFF);
}

TinyLiveDataField parseLiveDataField(const char* value) {
    if (!value) {
        return TinyLiveDataField::None;
    }
    String field = value;
    field.trim();
    field.toUpperCase();
    if (field == "VOLTAGE") return TinyLiveDataField::Voltage;
    if (field == "CURRENT") return TinyLiveDataField::Current;
    if (field == "SOCPERCENT" || field == "SOC" || field == "STATEOFCHARGE") return TinyLiveDataField::SocPercent;
    if (field == "SOHPERCENT" || field == "SOH" || field == "STATEOFHEALTH") return TinyLiveDataField::SohPercent;
    if (field == "TEMPERATURE") return TinyLiveDataField::Temperature;
    if (field == "MINCELLMV") return TinyLiveDataField::MinCellMv;
    if (field == "MAXCELLMV") return TinyLiveDataField::MaxCellMv;
    if (field == "BALANCINGBITS") return TinyLiveDataField::BalancingBits;
    if (field == "MAXCHARGECURRENT") return TinyLiveDataField::MaxChargeCurrent;
    if (field == "MAXDISCHARGECURRENT") return TinyLiveDataField::MaxDischargeCurrent;
    if (field == "ONLINESTATUS") return TinyLiveDataField::OnlineStatus;
    if (field == "NEEDBALANCING") return TinyLiveDataField::NeedBalancing;
    if (field == "CELLIMBALANCEMV" || field == "IMBALANCE") return TinyLiveDataField::CellImbalanceMv;
    return TinyLiveDataField::None;
}

VictronFieldEncoding parseEncoding(const char* value) {
    if (!value) {
        return VictronFieldEncoding::Unsigned;
    }
    String encoding = value;
    encoding.trim();
    encoding.toLowerCase();
    if (encoding == "signed" || encoding == "int" || encoding == "int16") {
        return VictronFieldEncoding::Signed;
    }
    if (encoding == "bits" || encoding == "bit") {
        return VictronFieldEncoding::Bits;
    }
    return VictronFieldEncoding::Unsigned;
}

VictronFieldEndianness parseEndianness(const char* value) {
    if (!value) {
        return VictronFieldEndianness::Little;
    }
    String endianness = value;
    endianness.trim();
    endianness.toLowerCase();
    if (endianness == "big" || endianness == "be") {
        return VictronFieldEndianness::Big;
    }
    return VictronFieldEndianness::Little;
}

VictronValueSourceType parseSourceType(const char* value) {
    if (!value) {
        return VictronValueSourceType::Unknown;
    }
    String type = value;
    type.trim();
    type.toLowerCase();
    if (type == "live_data" || type == "livedata") {
        return VictronValueSourceType::LiveData;
    }
    if (type == "function" || type == "compute") {
        return VictronValueSourceType::Function;
    }
    if (type == "constant") {
        return VictronValueSourceType::Constant;
    }
    return VictronValueSourceType::Unknown;
}

#ifdef ARDUINO
bool parseFieldFromJson(const JsonObjectConst& jsonField, VictronCanFieldDefinition& field, Logger* logger) {
    field.name = jsonField["name"].as<const char*>();
    field.byte_offset = jsonField["byte_offset"].as<uint8_t>();
    field.length = jsonField["length"].as<uint8_t>();
    field.bit_offset = jsonField["bit_offset"].as<uint8_t>();
    field.bit_length = jsonField["bit_length"].as<uint8_t>();
    field.encoding = parseEncoding(jsonField["encoding"].as<const char*>());
    field.endianness = parseEndianness(jsonField["endianness"].as<const char*>());

    JsonObjectConst source = jsonField["source"].as<JsonObjectConst>();
    field.source.type = parseSourceType(source["type"].as<const char*>());
    field.source.identifier = source["field"].as<const char*>();
    if (field.source.identifier.isEmpty()) {
        field.source.identifier = source["id"].as<const char*>();
    }
    if (field.source.type == VictronValueSourceType::LiveData) {
        field.source.live_field = parseLiveDataField(field.source.identifier.c_str());
    } else if (field.source.type == VictronValueSourceType::Constant) {
        field.source.constant = source["value"].as<float>();
    }

    JsonObjectConst conversion = jsonField["conversion"].as<JsonObjectConst>();
    if (!conversion.isNull()) {
        if (conversion.containsKey("gain")) {
            field.conversion.gain = conversion["gain"].as<float>();
        }
        if (conversion.containsKey("offset")) {
            field.conversion.offset = conversion["offset"].as<float>();
        }
        if (conversion.containsKey("round")) {
            field.conversion.round = conversion["round"].as<bool>();
        }
        if (conversion.containsKey("min")) {
            field.conversion.has_min = true;
            field.conversion.min_value = conversion["min"].as<float>();
        }
        if (conversion.containsKey("max")) {
            field.conversion.has_max = true;
            field.conversion.max_value = conversion["max"].as<float>();
        }
    }

    if (field.encoding != VictronFieldEncoding::Bits && field.length == 0) {
        field.length = 2; // default to 2 bytes for numeric fields
    }
    if (field.encoding == VictronFieldEncoding::Bits && field.bit_length == 0) {
        field.bit_length = 2; // default to 2-bit fields
    }

    if (field.source.type == VictronValueSourceType::LiveData && field.source.live_field == TinyLiveDataField::None) {
        if (logger) {
            logger->log(LOG_WARN, String("[CAN_MAP] Unknown live data field: ") + field.source.identifier);
        }
        return false;
    }

    if (field.encoding == VictronFieldEncoding::Bits && field.bit_length > 8) {
        if (logger) {
            logger->log(LOG_WARN, String("[CAN_MAP] Bit length too large for field: ") + field.name);
        }
        return false;
    }

    if (field.endianness == VictronFieldEndianness::Big && field.length > 0) {
        if (logger) {
            logger->log(LOG_WARN, String("[CAN_MAP] Big endian fields not supported: ") + field.name);
        }
        return false;
    }

    return true;
}
#endif

} // namespace

bool initializeVictronCanMapping(fs::FS& fs, const char* path, Logger* logger) {
#ifdef ARDUINO
    File file = fs.open(path, "r");
    if (!file) {
        if (logger) {
            logger->log(LOG_WARN, String("[CAN_MAP] File not found: ") + path);
        }
        return false;
    }
    String json = file.readString();
    file.close();
    return loadVictronCanMappingFromJson(json.c_str(), logger);
#else
    (void)fs;
    (void)path;
    (void)logger;
    return false;
#endif
}

bool loadVictronCanMappingFromJson(const char* json, Logger* logger) {
    if (!json) {
        if (logger) {
            logger->log(LOG_ERROR, "[CAN_MAP] JSON buffer is null");
        }
        return false;
    }

    auto previous = g_pgn_definitions;

#ifdef ARDUINO
    StaticJsonDocument<8192> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        if (logger) {
            logger->log(LOG_ERROR, String("[CAN_MAP] Failed to parse JSON: ") + err.c_str());
        }
        g_pgn_definitions = std::move(previous);
        return false;
    }

    JsonArrayConst mappings = doc["victron_can_mappings"].as<JsonArrayConst>();
    if (mappings.isNull()) {
        if (logger) {
            logger->log(LOG_ERROR, "[CAN_MAP] 'victron_can_mappings' missing or invalid");
        }
        g_pgn_definitions = std::move(previous);
        return false;
    }

    g_pgn_definitions.clear();
    g_pgn_definitions.reserve(mappings.size());

    for (JsonObjectConst pgnObj : mappings) {
        bool ok = false;
        uint16_t pgn = parsePgnId(pgnObj["pgn"].as<const char*>(), ok);
        if (!ok) {
            if (logger) {
                logger->log(LOG_WARN, "[CAN_MAP] Skipping PGN with invalid id");
            }
            continue;
        }

        VictronPgnDefinition def;
        def.pgn = pgn;
        def.name = pgnObj["name"].as<const char*>();

        JsonArrayConst fields = pgnObj["fields"].as<JsonArrayConst>();
        if (fields.isNull()) {
            if (logger) {
                logger->log(LOG_WARN, String("[CAN_MAP] PGN 0x") + String(pgn, HEX) + " has no fields");
            }
            continue;
        }

        def.fields.reserve(fields.size());
        for (JsonObjectConst fieldObj : fields) {
            VictronCanFieldDefinition field;
            if (!parseFieldFromJson(fieldObj, field, logger)) {
                continue;
            }
            def.fields.push_back(std::move(field));
        }

        if (!def.fields.empty()) {
            g_pgn_definitions.push_back(std::move(def));
        } else if (logger) {
            logger->log(LOG_WARN, String("[CAN_MAP] PGN 0x") + String(pgn, HEX) + " has zero valid fields");
        }
    }

    if (logger) {
        logger->log(LOG_INFO, String("[CAN_MAP] Loaded ") + String(g_pgn_definitions.size()) + " PGN definitions");
    }

    return !g_pgn_definitions.empty();
#else
    (void)logger;
    g_pgn_definitions = std::move(previous);
    return false;
#endif
}

const std::vector<VictronPgnDefinition>& getVictronPgnDefinitions() {
    return g_pgn_definitions;
}

const VictronPgnDefinition* findVictronPgnDefinition(uint16_t pgn) {
    for (const auto& def : g_pgn_definitions) {
        if (def.pgn == pgn) {
            return &def;
        }
    }
    return nullptr;
}

String victronValueSourceTypeToString(VictronValueSourceType type) {
    switch (type) {
        case VictronValueSourceType::LiveData:
            return "live_data";
        case VictronValueSourceType::Function:
            return "function";
        case VictronValueSourceType::Constant:
            return "constant";
        default:
            return "unknown";
    }
}

String tinyLiveDataFieldToString(TinyLiveDataField field) {
    switch (field) {
        case TinyLiveDataField::Voltage:
            return "Voltage";
        case TinyLiveDataField::Current:
            return "Current";
        case TinyLiveDataField::SocPercent:
            return "SocPercent";
        case TinyLiveDataField::SohPercent:
            return "SohPercent";
        case TinyLiveDataField::Temperature:
            return "Temperature";
        case TinyLiveDataField::MinCellMv:
            return "MinCellMv";
        case TinyLiveDataField::MaxCellMv:
            return "MaxCellMv";
        case TinyLiveDataField::BalancingBits:
            return "BalancingBits";
        case TinyLiveDataField::MaxChargeCurrent:
            return "MaxChargeCurrent";
        case TinyLiveDataField::MaxDischargeCurrent:
            return "MaxDischargeCurrent";
        case TinyLiveDataField::OnlineStatus:
            return "OnlineStatus";
        case TinyLiveDataField::NeedBalancing:
            return "NeedBalancing";
        case TinyLiveDataField::CellImbalanceMv:
            return "CellImbalanceMv";
        case TinyLiveDataField::None:
        default:
            return "None";
    }
}

String victronFieldEncodingToString(VictronFieldEncoding encoding) {
    switch (encoding) {
        case VictronFieldEncoding::Signed:
            return "signed";
        case VictronFieldEncoding::Bits:
            return "bits";
        case VictronFieldEncoding::Unsigned:
        default:
            return "unsigned";
    }
}

