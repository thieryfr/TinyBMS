# TinyBMS Architecture - Quick Reference

## 1. Module Dependencies Map

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
├─────────────────────────────────────────────────────────────┤
│  web_routes_api.cpp         ⚠️  Uses AsyncWebServer        │
│  websocket_handlers.cpp      ⚠️  Uses AsyncWebServer        │
│  tinybms_config_editor.cpp   ✅  Config + Logger            │
└────────────────┬──────────────┬─────────────┬───────────────┘
                 │              │             │
        ┌────────▼──────────────▼─────┬───────▼────────────┐
        │   Core Services Layer        │  Web Server Layer  │
        ├──────────────────────────────┼────────────────────┤
        │ ✅ event_bus_v2.cpp         │ ❌ ESPAsyncWebServer
        │ ✅ config_manager.cpp       │ ❌ AsyncTCP
        │ ✅ cvl_logic.cpp            │
        │ ✅ watchdog_manager.cpp     │
        │ ✅ logger.cpp               │
        │ ✅ mqtt/mqtt_bridge.cpp     │
        └────────┬──────────────┬─────┴────────┬────────────┘
                 │              │              │
        ┌────────▼──────────────▼──────────────▼────────────┐
        │         HAL Abstraction Layer (IHal*)             │
        ├─────────────────────────────────────────────────────┤
        │ ✅ ihal_uart.h         │ ✅ ihal_can.h            │
        │ ✅ ihal_storage.h      │ ✅ ihal_gpio.h           │
        │ ✅ ihal_timer.h        │ ✅ ihal_watchdog.h       │
        └────────┬────────────────────────────┬──────────────┘
                 │                            │
        ┌────────▼────────────┐      ┌────────▼────────────┐
        │  ESP32 HAL Impl     │      │  Mock HAL Impl      │
        ├─────────────────────┤      ├────────────────────┤
        │ esp32_uart.cpp      │      │ mock_uart.cpp      │
        │ esp32_can.cpp       │      │ mock_can.cpp       │
        │ esp32_storage.cpp   │      │ mock_storage.cpp   │
        │ esp32_gpio.cpp      │      │ mock_gpio.cpp      │
        │ esp32_timer.cpp     │      │ mock_timer.cpp     │
        │ esp32_watchdog.cpp  │      │ mock_watchdog.cpp  │
        └────────┬────────────┘      └────────┬───────────┘
                 │                            │
        ┌────────▼────────────────────────────▼───────────┐
        │     Hardware / External Services                │
        ├─────────────────────────────────────────────────┤
        │ UART: TinyBMS      │ SPIFFS: config.json       │
        │ CAN: Victron GX    │ WiFi: STA/AP mode         │
        │ FreeRTOS: Tasks    │ MQTT: Optional broker     │
        └─────────────────────────────────────────────────┘

Legend:
✅ = Framework-independent, no migration needed
⚠️ = Requires HAL abstraction refresh
❌ = Requires major rewrite for ESP-IDF
```

---

## 2. Task Execution Model

```
FreeRTOS Core 0 (80MHz)           FreeRTOS Core 1 (160MHz)
├─ WiFi/BT Stack (SYS)            ├─ UART Task
├─ CVL Task (20s, NORMAL)         │  └─ 10Hz polling
├─ WebSocket Task (1Hz)           ├─ CAN Task
├─ Web Server Task (async)        │  └─ 1Hz transmission
│  └─ http_server                 └─ System idle
└─ MQTT Task (optional)

Synchronization Points:
━━━━━━━━━━━━━━━━━━━━━━
EventBus (pub/sub) → All tasks subscribe to live data
configMutex       → Configuration reads (100ms timeout)
uartMutex         → UART hardware access (100ms timeout)
feedMutex         → Watchdog feeding (100ms timeout)
statsMutex        → Bridge statistics (10ms timeout)
```

---

## 3. Data Flow Sequence (10Hz Update Cycle)

```
Time: 0ms      ┌─────────────────┐
               │ UART Task Wakes  │
               └────────┬─────────┘
                        │
Time: 10ms             ├─► Read 6 TinyBMS blocks (Modbus RTU)
                        │   └─ 70-80ms typical latency
                        │
Time: 80ms             ├─► Parse & validate (CRC check)
                        │
Time: 85ms             ├─► Publish LiveDataUpdate → EventBus
                        │   └─ Updates cache instantly
Time: 86ms             │
                        ├─► Publish MqttRegisterValue events
                        │   └─ One event per register
                        │
Time: 100ms    ┌────────▼──────────────┐
               │ EventBus Notifies      │
               │ All Subscribers        │
               └──────┬─────┬──────┬────┘
                      │     │      │
                      ▼     ▼      ▼
                   CAN    WS      MQTT
                  Task   Task     Task
                   │      │       │
Time: 102ms    ┌───▼──────▼──────▼──┐
               │ CAN: Send 9 PGNs   │  (every 1000ms)
               │ WS: Broadcast JSON │  (every 1000ms)
               │ MQTT: Publish     │  (on change)
               └───────────────────┘
```

---

## 4. Configuration Hierarchy & Hot-Reload

```
┌──────────────────────────────────┐
│       /data/config.json          │  (SPIFFS, persistent)
│  (9 sections, 80+ parameters)    │
└──────────────┬───────────────────┘
               │
        ┌──────▼─────────┐
        │ ConfigManager  │
        │  (struct)      │
        ├────────────────┤
        │ • wifi.*       │  All loaded into RAM at boot
        │ • hardware.*   │
        │ • tinybms.*    │  Protected by configMutex
        │ • victron.*    │
        │ • cvl.*        │  Hot-reload via:
        │ • mqtt.*       │  POST /api/settings {json}
        │ • web_server.* │
        │ • logging.*    │  Immediate effect (no reboot)
        │ • advanced.*   │
        └──────┬─────────┘
               │
        ┌──────▼──────────────┐
        │ Config Change Event │
        │ (EventBus publish)  │
        └─────────────────────┘
```

---

## 5. Latency Measurements (Current Baseline)

```
┌─────────────────────────────────────────────────────────┐
│  Metric                    Current    Goal for ESP-IDF  │
├─────────────────────────────────────────────────────────┤
│ UART Poll → Publish        70-80ms    <100ms           │
│ Publish → CAN TX           2-5ms      <10ms            │
│ Publish → WebSocket TX     80-120ms   <200ms (slack)   │
│ Config Update → Applied    <50ms      <50ms            │
│ Watchdog Feed              2Hz        2Hz (no change)  │
└─────────────────────────────────────────────────────────┘
```

---

## 6. Memory Usage Profile

```
Resource                Current      ESP-IDF Target
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Heap Free (min)        180-220KB    >150KB
Stack HWM              ~100KB       <150KB
EventBus Queue         32 slots     32 slots
Max WebSocket Clients  4            4-8 (configurable)
SPIFFS Size            1MB          1MB
Binary Size            ~500KB       ~600-700KB (ESP-IDF larger)
```

---

## 7. Library Dependency Matrix

```
Library              Type    Portable  Replacement for ESP-IDF
────────────────────────────────────────────────────────
ArduinoJson v6.21.0  JSON    ✅ YES    Keep as-is
ESPAsyncWebServer    WEB     ❌ NO     esp_http_server
AsyncTCP             TCP     ❌ NO     lwip (in esp_http_server)
CAN v0.3.1           DRIVER  ⚠️ MAYBE  twai_driver
Arduino (UART/SPI)   CORE    ⚠️ MAYBE  esp-idf uart/spi drivers
WiFi (Arduino)       STACK   ❌ NO     esp_wifi API

Critical Path: Need to replace AsyncWebServer + WiFi
for 100% native ESP-IDF build
```

---

## 8. Build Configuration Comparison

### Current (PlatformIO + Arduino)
```ini
[env:esp32can]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = ArduinoJson@^6.21.0, ESPAsyncWebServer, ...
upload_speed = 921600
monitor_speed = 115200
build_flags = -DCORE_DEBUG_LEVEL=3 -O2 -std=c++17
```

### Target (Native ESP-IDF with CMake)
```cmake
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tinybms)

idf_build_process(esp32
    COMPONENTS
        main
        hal
        events
        config
        mqtt
    SDKCONFIG "sdkconfig"
)
```

---

## 9. Critical Success Criteria

### Phase 1 (HAL Implementation)
- [ ] All 6 HAL interface implementations complete
- [ ] CMakeLists.txt builds successfully
- [ ] Unit tests pass (CVL, Config, etc.)
- [ ] No functional regression

### Phase 2 (Web Stack)
- [ ] esp_http_server compiles
- [ ] REST endpoints respond correctly
- [ ] WebSocket connections established
- [ ] JSON payloads valid
- [ ] Latency <200ms (WebSocket)

### Phase 3 (Polish)
- [ ] 24-hour stability test passed
- [ ] All metrics preserved
- [ ] Documentation updated
- [ ] Production readiness confirmed

---

## 10. Quick Navigation

**For Architecture Questions:**
- `/docs/architecture.md` - Detailed patterns & principles

**For Module Details:**
- `/docs/README_*.md` - Module-specific guides (18+ files)

**For Migration Planning:**
- `/docs/ESP-IDF_MIGRATION_SUMMARY.md` - Executive view
- `/docs/ESP-IDF_MIGRATION_ANALYSIS.md` - Technical deep-dive

**For Code Review:**
- `/include/*.h` - Header interfaces
- `/src/*.cpp` - Implementations

**For Testing:**
- `/tests/native/` - C++ unit tests
- `/tests/integration/` - Python end-to-end tests

