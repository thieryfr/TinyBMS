# MQTT Topic Regression & Venus OS Validation

## 1. Automated regression coverage

| Scope | Command | Purpose |
| --- | --- | --- |
| Unit (CVL logic) | `g++ -std=c++17 -Iinclude tests/test_cvl_logic.cpp src/cvl_logic.cpp -o /tmp/test_cvl && /tmp/test_cvl` | Guards the pure CVL state machine used by the MQTT bridge to publish charge/discharge limits. |
| Integration (topic regression) | `python -m pytest tests/integration/test_mqtt_topic_regression.py -v` | Verifies that the recorded MQTT topic catalogue stayed backward compatible and tracks the declared new topics. |
| Integration (end-to-end) | `python -m pytest tests/integration/test_end_to_end_flow.py -v` | Ensures the UART → EventBus → CAN/Web pipeline keeps emitting the fields consumed by MQTT and Venus OS dashboards. |

All commands above execute without hardware by relying on recorded fixtures (`tests/fixtures/*.json`).

## 2. Topic catalogue diff

The snapshot fixture [`tests/fixtures/mqtt_topics_snapshot.json`](../../tests/fixtures/mqtt_topics_snapshot.json) compares the previous (`before`) and updated (`after`) MQTT catalogues.

### Topics preserved (compatibilité ascendante)

| Topic suffix | Rationale |
| --- | --- |
| `battery_pack_voltage` | Core DC voltage, consumed by `/Dc/0/Voltage`. |
| `battery_pack_current` | Charge/discharge current consumed by `/Dc/0/Current`. |
| `internal_temperature` | Internal sensor, reused for `/Dc/0/Temperature`. |
| `state_of_charge` | SOC `%` feed for Victron `/Soc`. |
| `state_of_health` | Long-term SOH published under `/Soh`. |
| `max_charge_current` | Charge limit for ESS logic. |
| `max_discharge_current` | Discharge limit for ESS logic. |
| `overvoltage_cutoff_mv` | Diagnostic threshold (unchanged JSON schema). |
| `undervoltage_cutoff_mv` | Diagnostic threshold (unchanged JSON schema). |
| `discharge_overcurrent_a` | Protective threshold forwarded to dashboards. |
| `charge_overcurrent_a` | Protective threshold forwarded to dashboards. |
| `overheat_cutoff_c` | Thermal safety threshold forwarded to dashboards. |

### New topics (Venus OS surfacing)

| Topic suffix | DBus path | Notes |
| --- | --- | --- |
| `pack_power_w` | `/Dc/0/Power` | Derived from voltage × current, allows Victron GX to display pack power directly. |
| `system_state` | `/System/0/State` | Mirrors the TinyBMS online status into the Venus system state enumeration. |

## 3. Venus OS / ESS functional validation

A 10-minute soak test was executed on a GX running **Venus OS v3.61 / ESS 1.16** with the bridge firmware built from this branch. The resulting capture is stored in [`tests/fixtures/venus_os_validation.json`](../../tests/fixtures/venus_os_validation.json) and cross-checked by `test_mqtt_topic_regression.py`.

Key observations:

- Legacy MQTT topics remained visible without any configuration change (root topic `victron/tinybms`).
- Newly introduced `pack_power_w` and `system_state` topics mapped to the expected DBus nodes and produced consistent payload samples.
- No alarms or regressions were reported by the GX; MQTT connectivity remained stable during the soak.

The fixtures and automated checks above provide the evidence of backward compatibility and successful Venus OS integration for this release.
