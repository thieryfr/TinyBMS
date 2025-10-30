#include "victron_alarm_utils.h"

#include <cstring>

using tinybms::events::AlarmCode;
using tinybms::events::AlarmEvent;
using tinybms::events::AlarmSeverity;

namespace victron {
namespace {
struct AlarmMapping {
    AlarmCode code;
    AlarmBit bit;
    const char* path;
};

constexpr AlarmMapping kMappings[] = {
    {AlarmCode::UnderVoltage, AlarmBit::UnderVoltage, "/Alarms/LowVoltage"},
    {AlarmCode::OverVoltage, AlarmBit::OverVoltage, "/Alarms/HighVoltage"},
    {AlarmCode::OverTemperature, AlarmBit::OverTemperature, "/Alarms/HighTemperature"},
    {AlarmCode::LowTempCharge, AlarmBit::LowTempCharge, "/Alarms/LowTemperatureCharge"},
    {AlarmCode::CellImbalance, AlarmBit::CellImbalance, "/Alarms/CellImbalance"},
    {AlarmCode::CanTxError, AlarmBit::CommsError, "/Alarms/Communication"},
    {AlarmCode::CanTimeout, AlarmBit::CommsError, "/Alarms/Communication"},
    {AlarmCode::CanError, AlarmBit::CommsError, "/Alarms/Communication"},
    {AlarmCode::CanKeepAliveLost, AlarmBit::CommsError, "/Alarms/Communication"},
    {AlarmCode::UartError, AlarmBit::CommsError, "/Alarms/Communication"},
    {AlarmCode::UartTimeout, AlarmBit::CommsError, "/Alarms/Communication"},
    {AlarmCode::BmsFault, AlarmBit::Shutdown, "/Alarms/SystemShutdown"},
    {AlarmCode::BmsOffline, AlarmBit::Shutdown, "/Alarms/SystemShutdown"},
    {AlarmCode::WatchdogReset, AlarmBit::Shutdown, "/Alarms/SystemShutdown"},
};

uint8_t clampLevel(uint8_t level) {
    return (level > 2u) ? 2u : level;
}

} // namespace

uint8_t severityToVictronLevel(AlarmSeverity severity) {
    switch (severity) {
        case AlarmSeverity::Info:
            return 0u;
        case AlarmSeverity::Warning:
            return 1u;
        case AlarmSeverity::Error:
        case AlarmSeverity::Critical:
            return 2u;
    }
    return 0u;
}

bool annotateAlarm(AlarmCode code, AlarmSeverity severity, AlarmEvent& alarm) {
    alarm.victron_bit = 255u;
    alarm.victron_level = severityToVictronLevel(severity);
    alarm.victron_path[0] = '\0';

    for (const auto& mapping : kMappings) {
        if (mapping.code == code) {
            alarm.victron_bit = static_cast<uint8_t>(mapping.bit);
            alarm.victron_level = clampLevel(alarm.victron_level);
            if (mapping.path) {
                std::strncpy(alarm.victron_path, mapping.path, sizeof(alarm.victron_path) - 1);
                alarm.victron_path[sizeof(alarm.victron_path) - 1] = '\0';
            }
            return true;
        }
    }

    return false;
}

} // namespace victron

