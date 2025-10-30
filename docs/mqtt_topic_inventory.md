# MQTT Topic Inventory and TinyBMS ↔️ Venus Mapping

## 1. MQTT producers and topic structure
- The MQTT client is disabled by default but ships with a deterministic configuration block: root topic `victron/tinybms`, QoS `0`, no retain, standard port `1883` and `keepalive` 30 s.【F:include/config_manager.h†L119-L134】 When enabled, `VictronMqttBridge::configure()` sanitises the configured root topic (lowercase segments, stripping unsafe characters) before composing any publication path.【F:src/mqtt/victron_mqtt_bridge.cpp†L144-L149】【F:src/mqtt/victron_mqtt_bridge.cpp†L46-L73】【F:src/mqtt/victron_mqtt_bridge.cpp†L397-L410】
- `TinyBMS_Victron_Bridge::uartTask()` polls TinyBMS blocks, resolves runtime bindings and queues `MqttRegisterValue` events after guaranteeing that the full live snapshot has been published first.【F:src/bridge_uart.cpp†L235-L412】 Each loop iteration sleeps for `uart_poll_interval_ms_`, whose adaptive scheduler is initialised from the TinyBMS defaults (100 ms base, 50 ms min, 500 ms max).【F:src/bridge_uart.cpp†L500-L511】【F:include/config_manager.h†L56-L61】
- `VictronMqttBridge` subscribes to those events, enriches them with read/write metadata (keys, units, precision) and builds a JSON payload that carries raw words, scaled values, optional text, comments and labels before publishing to `<root_topic>/<suffix>` with the configured QoS/retain policy.【F:src/mqtt/register_value.cpp†L9-L133】【F:src/mqtt/victron_mqtt_bridge.cpp†L124-L150】【F:src/mqtt/victron_mqtt_bridge.cpp†L300-L357】 The topic suffix is derived from RW keys, fallback labels or register addresses via the same sanitiser, ensuring topic stability.【F:src/mqtt/register_value.cpp†L9-L133】

## 2. TinyBMS UART command frames (Rev D)
Key frames referenced by the bridge are summarised below (see the extracted Rev D protocol for byte-level details):
- **Acknowledgement / error handling** – command `0xAA 0x00/0x01` signalling CRC or command errors vs. ACK confirmation.【F:docs/TinyBMS_Communication_Protocols_Rev_D.txt†L480-L514】 
- **Register block read** – request `0xAA 0x07 RL ADDR_L ADDR_H CRC` followed by an `[OK]` reply containing `PL` words, the raw register payload used by the bindings.【F:docs/TinyBMS_Communication_Protocols_Rev_D.txt†L515-L539】 
- **Metadata handshake** – request `0xAA 0x1F` returning hardware version, firmware versions, bootloader and the register-map revision (DATA6), which the bridge stores through binding metadata.【F:docs/TinyBMS_Communication_Protocols_Rev_D.txt†L2087-L2100】

## 3. Venus OS Modbus register expectations (GX 3.60)
The Venus Modbus TCP list exposes the following battery metrics that align with TinyBMS telemetry (addresses are decimal):

| Description | Address | Type / scale | DBus path | Remarks |
| --- | --- | --- | --- | --- |
| Battery power | 256 | `int32`, scale 1 | `/Dc/0/Power` | 32-bit variant for high-power systems.
| Battery power (legacy) | 258 | `int16`, scale 1 | `/Dc/0/Power` | 16-bit fallback.
| Battery voltage | 259 | `uint16`, scale ÷100 | `/Dc/0/Voltage` | Published in 0.01 V increments.
| Starter voltage | 260 | `uint16`, scale ÷100 | `/Dc/1/Voltage` | Optional aux battery.
| Battery current | 261 | `int16`, scale ÷10 | `/Dc/0/Current` | Positive = charging.
| Battery temperature | 262 | `int16`, scale ÷10 | `/Dc/0/Temperature` | Degrees Celsius.
| State of charge | 266 | `uint16`, scale ÷10 | `/Soc` | 0.1 % resolution.
| State of health | 304 | `uint16`, scale ÷10 | `/Soh` | Long-term health metric.

## 4. MQTT ↔ TinyBMS ↔ Venus mapping
The table below lists the high-value topics produced today, the source registers and conversions applied by the bridge, and the Venus OS expectations where available. Publish cadence inherits the adaptive UART poller (100 ms default) and no API surface is altered — documentation only.

| Topic suffix | TinyBMS register(s) | Conversion | Venus OS register | Notes |
| --- | --- | --- | --- | --- |
| `battery_pack_voltage` | 36 (raw 36) | Float × 0.01 | Addr 259 (uint16 ÷ 100) `/Dc/0/Voltage` | Battery Pack Voltage |
| `battery_pack_current` | 38 (raw 38) | Float × 0.1 | Addr 261 (int16 ÷ 10) `/Dc/0/Current` | Battery Pack Current |
| `internal_temperature` | 48 (raw 48) | Int16 × 0.1 | Addr 262 (int16 ÷ 10) `/Dc/0/Temperature` | Internal Temperature |
| `state_of_charge` | 46 (raw 46) | Uint16 × 0.1 | Addr 266 (uint16 ÷ 10) `/Soc` | State Of Charge |
| `state_of_health` | 45 (raw 45) | Uint16 × 0.1 | Addr 304 (uint16 ÷ 10) `/Soh` | State Of Health |
| `max_charge_current` | 103 (raw 103) | Uint16 × 0.1 | — | Max Charge Current |
| `max_discharge_current` | 102 (raw 102) | Uint16 × 0.1 | — | Max Discharge Current |
| `overvoltage_cutoff_mv` | 315 (raw 315) | Uint16 × 1.0 | — | Overvoltage Cutoff |
| `undervoltage_cutoff_mv` | 316 (raw 316) | Uint16 × 1.0 | — | Undervoltage Cutoff |
| `discharge_overcurrent_a` | 317 (raw 317) | Uint16 × 1.0 | — | Discharge Over-current Cutoff |
| `charge_overcurrent_a` | 318 (raw 318) | Uint16 × 1.0 | — | Charge Over-current Cutoff |
| `overheat_cutoff_c` | 319 (raw 319) | Uint16 × 1.0 | — | Overheat Cutoff |

All other topics retain their existing names and JSON schema; documenting them here introduces no breaking change for downstream MQTT consumers.
