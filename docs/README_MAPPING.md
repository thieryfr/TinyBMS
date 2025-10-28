+++README_MAPPING.md
@@ -0,0 +1,12 @@
+# Bridge Module Mapping
+
+| Path | Description |
+| --- | --- |
+| `src/bridge_core.cpp` | Initializes the TinyBMS ↔ Victron bridge, applies configuration driven intervals, and spawns UART/CAN/CVL tasks. |
+| `src/bridge_uart.cpp` | Polls TinyBMS data, publishes it on the Event Bus, and raises alarms using dynamic thresholds from `ConfigManager`. |
+| `src/bridge_can.cpp` | Builds and transmits Victron PGNs, emits the 0x35A alarms frame with configurable thresholds, and sends CAN keep-alive frames. |
+| `src/bridge_keepalive.cpp` | Tracks Victron keep-alive reception, updates CAN watchdog statistics, and raises alarms on timeout. |
+| `include/tinybms_victron_bridge.h` | Declares the bridge facade, shared statistics, and TinyBMS configuration snapshot used by the tasks. |
+| `include/config_manager.h` / `src/config_manager.cpp` | Load/save configuration (including Victron alarm thresholds and keep-alive timings) from SPIFFS with mutex protection. |
+| `include/watchdog_manager.h` / `src/watchdog_manager.cpp` | Hardware watchdog abstraction with statistics, API accessors, and task helper. |
+| `include/can_driver.h` / `src/can_driver.cpp` | Stub CAN driver abstraction used by the bridge modules. |
 
EOF
)
