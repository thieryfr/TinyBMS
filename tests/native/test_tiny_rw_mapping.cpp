#include <cassert>
#include <cmath>
#include <Arduino.h>

#include "tiny_rw_mapping.h"

namespace {

const char* SAMPLE_JSON = R"JSON({
    "tiny_rw_registers": {
        "100": {
            "key": "charge_voltage",
            "label": "Charge Voltage",
            "unit": "V",
            "type": "uint16",
            "group": "charger",
            "comment": "Target charger voltage",
            "scale": 0.1,
            "precision": 0,
            "min": 420,
            "max": 580,
            "step": 2,
            "default": 546
        },
        "101": {
            "key": "balancing_mode",
            "label": "Balancing Mode",
            "unit": "mode",
            "type": "enum",
            "group": "system",
            "comment": "Balancing algorithm",
            "enum": [
                { "value": 0, "label": "Off" },
                { "value": 1, "label": "Auto" }
            ],
            "default": 1
        },
        "102": {
            "key": "current_offset",
            "label": "Current Offset",
            "unit": "A",
            "type": "int16",
            "group": "advanced",
            "comment": "Signed calibration offset",
            "scale": 0.01,
            "precision": 2,
            "min": -500,
            "max": 500,
            "default": 0
        }
    }
})JSON";

} // namespace

int main() {
    bool ok = loadTinyRwMappingFromJson(SAMPLE_JSON, nullptr);
    assert(ok);

    const auto& regs = getTinyRwRegisters();
    assert(regs.size() == 3);

    const TinyRwRegisterMetadata* charge = findTinyRwRegister(100);
    assert(charge != nullptr);
    assert(charge->key == String("charge_voltage"));
    assert(charge->scale == 0.1f);
    assert(charge->precision >= 1); // auto precision due to scale < 1
    assert(charge->has_min);
    assert(std::abs(charge->min_value - 42.0f) < 0.001f);
    assert(charge->has_max);
    assert(std::abs(charge->max_value - 58.0f) < 0.001f);

    uint16_t raw = 0;
    bool converted = tinyRwConvertUserToRaw(*charge, 54.6f, raw);
    assert(converted);
    assert(raw == 546);
    float back = tinyRwConvertRawToUser(*charge, raw);
    assert(std::abs(back - 54.6f) < 0.0001f);

    const TinyRwRegisterMetadata* mode = findTinyRwRegisterByKey(String("balancing_mode"));
    assert(mode != nullptr);
    assert(mode->value_class == TinyRegisterValueClass::Enum);
    assert(mode->enum_values.size() == 2);
    assert(mode->default_raw == 1);
    assert(mode->default_value == 1.0f);
    uint16_t enum_raw = 0;
    assert(tinyRwConvertUserToRaw(*mode, 1.0f, enum_raw));
    assert(enum_raw == 1);
    assert(!tinyRwConvertUserToRaw(*mode, 5.0f, enum_raw));

    const TinyRwRegisterMetadata* offset = findTinyRwRegister(102);
    assert(offset != nullptr);
    assert(offset->value_class == TinyRegisterValueClass::Int);
    assert(offset->has_min);
    assert(std::abs(offset->min_value - (-5.0f)) < 0.0001f);
    assert(offset->has_max);
    assert(std::abs(offset->max_value - 5.0f) < 0.0001f);
    assert(tinyRwConvertUserToRaw(*offset, -3.25f, raw));
    assert(static_cast<int16_t>(raw) == -325);
    assert(!tinyRwConvertUserToRaw(*offset, 600.0f, raw));

    size_t previous_count = regs.size();
    bool invalid = loadTinyRwMappingFromJson("{ invalid json", nullptr);
    assert(!invalid);
    assert(getTinyRwRegisters().size() == previous_count);

    return 0;
}
