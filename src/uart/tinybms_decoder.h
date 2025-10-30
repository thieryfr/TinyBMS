#pragma once

#include <Arduino.h>
#include <map>

#include "event/event_types_v2.h"
#include "shared_data.h"
#include "tiny_read_mapping.h"

namespace tinybms::uart::detail {

/**
 * @brief Decode a TinyBMS register binding and apply the result to the live snapshot.
 *
 * The helper encapsulates all legacy scaling rules so that both the UART task and
 * the unit tests exercise the exact same decoding logic. Keeping this helper in
 * the `src/` tree (and not under `include/`) ensures we do not expose a new
 * public API, which preserves backward compatibility for existing modules that
 * already rely on `TinyBMS_LiveData` and the event payload layout.
 *
 * @param binding         Register binding description (address, scaling, field).
 * @param register_values Map of raw Modbus words read during the polling round.
 * @param live_data       Mutable live data snapshot that receives the decoded value.
 * @param timestamp_ms    Capture timestamp propagated to MQTT register events.
 * @param mqtt_event_out  Optional pointer that receives a populated MQTT event when
 *                        the binding is successfully decoded. May be nullptr if the
 *                        caller is not interested in MQTT notifications.
 * @return true if all required Modbus words were present and the binding was decoded.
 */
bool decodeAndApplyBinding(const TinyRegisterRuntimeBinding& binding,
                           const std::map<uint16_t, uint16_t>& register_values,
                           TinyBMS_LiveData& live_data,
                           uint32_t timestamp_ms,
                           tinybms::events::MqttRegisterEvent* mqtt_event_out);

/**
 * @brief Apply derived calculations after raw bindings have been processed.
 *
 * The historic UART pipeline always filled `cell_imbalance_mv` and defaulted the
 * `online_status` flag once all registers were decoded. Keeping the behaviour in
 * a dedicated helper allows tests to demonstrate that the legacy API contracts are
 * still honoured after the refactor.
 */
void finalizeLiveDataFromRegisters(TinyBMS_LiveData& live_data);

} // namespace tinybms::uart::detail

