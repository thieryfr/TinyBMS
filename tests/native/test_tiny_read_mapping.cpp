#include <cassert>
#include <Arduino.h>

#include "tiny_read_mapping.h"

int main() {
    const char* sample_json = R"JSON({
        "tiny_read_registers": {
            "36": {
                "tiny_name": "Battery Pack Voltage",
                "tiny_type": "FLOAT",
                "tiny_scale_unit": "1 V",
                "comment": "Sample entry"
            },
            "38": {
                "tiny_name": "Battery Pack Current",
                "tiny_type": "FLOAT",
                "tiny_scale_unit": "0.1 A",
                "comment": "Sample entry"
            }
        }
    })JSON";

    bool ok = loadTinyReadMappingFromJson(sample_json, nullptr);
    assert(ok);

    const auto& metadata = getTinyRegisterMetadata();
    assert(metadata.size() == 2);

    const TinyRegisterMetadata* voltage = findTinyRegisterMetadata(36);
    assert(voltage != nullptr);
    assert(voltage->name.toStdString() == std::string("Battery Pack Voltage"));
    assert(voltage->type == TinyRegisterValueType::Float);

    const TinyRegisterMetadata* current = findTinyRegisterMetadata(38);
    assert(current != nullptr);
    assert(current->unit.toStdString() == std::string("0.1 A"));

    bool binding_found = false;
    for (const auto& binding : getTinyRegisterBindings()) {
        if (binding.metadata_address == 36) {
            assert(binding.metadata != nullptr);
            binding_found = true;
        }
    }
    assert(binding_found);

    size_t previous = metadata.size();
    bool invalid = loadTinyReadMappingFromJson("{ invalid json", nullptr);
    assert(!invalid);
    assert(getTinyRegisterMetadata().size() == previous);

    return 0;
}
