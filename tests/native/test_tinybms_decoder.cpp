#include <Arduino.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "event/event_types_v2.h"
#include "tiny_read_mapping.h"
#include "uart/tinybms_decoder.h"

using tinybms::events::MqttRegisterEvent;

int main() {
    {
        std::map<uint16_t, uint16_t> registers{
            {32, 0xABCD},   // lifetime counter LSW
            {33, 0x0001},   // lifetime counter MSW -> 0x0001ABCD
            {36, 5200},     // 52.00 V after scale 0.01
            {38, static_cast<uint16_t>(static_cast<int16_t>(-85))}, // -8.5 A with scale 0.1
            {40, 3100},
            {41, 3275},
            {45, 940},      // 94.0 % SOH
            {46, 815},      // 81.5 % SOC
            {48, static_cast<uint16_t>(250)}, // 25.0 °C
            {50, 0x0000},   // force fallback online status
            {51, 0x0003},
            {102, 450},     // 45.0 A discharge limit
            {103, 320},     // 32.0 A charge limit
            {113, static_cast<uint16_t>((15 << 8) | 4)}, // min=4°C, max=15°C
            {315, 3400},
            {316, 2800},
            {317, 120},
            {318, 100},
            {319, 55},
            {500, static_cast<uint16_t>(('T' << 8) | 'i')},
            {501, static_cast<uint16_t>(('n' << 8) | 'y')},
            {502, static_cast<uint16_t>(('B' << 8) | 'M')},
            {503, static_cast<uint16_t>(('S' << 8) | '\0')},
        };

        TinyBMS_LiveData live{};
        live.resetSnapshots();

        std::vector<MqttRegisterEvent> events;
        const auto& bindings = getTinyRegisterBindings();
        const uint32_t now = 123456;

        for (const auto& binding : bindings) {
            MqttRegisterEvent evt{};
            if (tinybms::uart::detail::decodeAndApplyBinding(binding, registers, live, now, &evt)) {
                events.push_back(evt);
            }
        }

        tinybms::uart::detail::finalizeLiveDataFromRegisters(live);

        assert(std::fabs(live.voltage - 52.0f) < 1e-6f);
        assert(std::fabs(live.current + 8.5f) < 1e-6f);
        assert(std::fabs(live.soc_percent - 81.5f) < 1e-6f);
        assert(std::fabs(live.soh_percent - 94.0f) < 1e-6f);
        assert(live.min_cell_mv == 3100);
        assert(live.max_cell_mv == 3275);
        assert(live.cell_imbalance_mv == static_cast<uint16_t>(3275 - 3100));
        assert(live.pack_temp_min == 40);   // 4.0°C -> 40 in 0.1°C units
        assert(live.pack_temp_max == 150);  // 15.0°C -> 150
        assert(live.max_discharge_current == 450);
        assert(live.max_charge_current == 320);
        assert(live.discharge_overcurrent_a == 120);
        assert(live.charge_overcurrent_a == 100);
        assert(live.overheat_cutoff_c == 55);
        assert(live.online_status == 0x91); // default applied because register 50 was zero

        const TinyRegisterSnapshot* lifetime = live.findSnapshot(32);
        assert(lifetime != nullptr);
        assert(lifetime->raw_value == 0x0001ABCD);

        const TinyRegisterSnapshot* manufacturer = live.findSnapshot(500);
        assert(manufacturer != nullptr);
        assert(manufacturer->text_value.toStdString() == std::string("TinyBMS"));

        const TinyRegisterSnapshot* firmware = live.findSnapshot(501);
        assert(firmware != nullptr);
        assert(firmware->text_value.toStdString() == std::string("28281.16973"));

        bool saw_soc_event = false;
        for (const auto& evt : events) {
            if (evt.address == 46) {
                saw_soc_event = true;
                assert(evt.raw_value == 815);
                assert(evt.has_text == false);
            }
            if (evt.address == 501) {
                assert(evt.has_text);
                assert(String(evt.text_value).toStdString() == std::string("28281.16973"));
            }
        }
        assert(saw_soc_event);

        assert(live.snapshotCount() >= events.size());
    }

    {
        std::map<uint16_t, uint16_t> empty_registers;
        TinyBMS_LiveData live{};
        live.resetSnapshots();
        const auto& bindings = getTinyRegisterBindings();
        for (const auto& binding : bindings) {
            if (tinybms::uart::detail::decodeAndApplyBinding(binding, empty_registers, live, 0, nullptr)) {
                assert(false && "No binding should decode without data");
            }
        }
        tinybms::uart::detail::finalizeLiveDataFromRegisters(live);
        assert(live.snapshotCount() == 0);
        assert(live.online_status == 0x91);
    }

    return 0;
}

