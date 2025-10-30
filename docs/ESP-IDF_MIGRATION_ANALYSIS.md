# TinyBMS Codebase Structure Analysis - ESP-IDF Migration Plan

## Executive Summary

**Project:** TinyBMS-Victron Bridge (v2.5.0)  
**Current State:** Production Ready (9.5/10)  
**Build System:** PlatformIO (Arduino framework on ESP32)  
**Total Code:** ~10,500 lines (C++)  
**Architecture Score:** Excellent (event-driven, HAL-abstracted, fully documented)

---

## 1. BUILD SYSTEM ANALYSIS

### 1.1 Current Build System: PlatformIO

**Configuration File:** `/home/user/TinyBMS/platformio.ini`

```ini
[platformio]
default_envs = esp32can

[env:esp32can]
platform = espressif32
board = esp32dev
framework = arduino
```

**Build Properties:**
- **Framework:** Arduino (not native ESP-IDF)
- **Platform:** espressif32 (PlatformIO wrapper)
- **Board:** esp32dev (generic ESP32)
- **Optimization:** `-O2` (standard optimization)
- **C++ Standard:** C++17 (`-std=c++17`)

**Key Build Flags:**
```
-DCORE_DEBUG_LEVEL=3
-DTINYBMS_UART_RX=16
-DTINYBMS_UART_TX=17
-DVICTRON_CAN_TX=5
-DVICTRON_CAN_RX=4
-DDEBUG_LEVEL=2
-DCONFIG_ARDUINO_LOOP_STACK_SIZE=8192
```

**Partition Configuration:**
- Custom partition table: `partitions.csv`
- SPIFFS filesystem: 1 MB (for config.json, web assets, logs)
- Firmware + filesystem flashable

**Upload Configuration:**
- Upload speed: 921600 baud
- Monitor speed: 115200 baud
- Serial monitoring with exception decoder

### 1.2 Current Build System Strengths

✅ Simple, straightforward configuration  
✅ Direct Arduino library compatibility  
✅ Minimal abstraction layer (straightforward debugging)  
✅ Fast incremental builds  
✅ Large community ecosystem  

### 1.3 ESP-IDF Migration Challenges Identified

⚠️ **Framework Differences:**
- Arduino is a wrapper around FreeRTOS/ESP-IDF
- Direct ESP-IDF would require:
  - Removing Arduino.h dependencies
  - Using esp-idf native peripherals (UART, CAN, storage)
  - Manual FreeRTOS task creation (currently abstracted by Arduino)
  - Different memory management patterns

⚠️ **Library Dependencies:**
- `ArduinoJson` - Cross-platform, works with ESP-IDF
- `ESPAsyncWebServer` - Arduino-specific, may need replacement
- `AsyncTCP` - Arduino-specific, may need replacement
- `CAN` - Arduino library, needs ESP-IDF CAN driver

⚠️ **Peripheral Access:**
- Currently: `Serial` (Arduino), `WiFi` (Arduino), `SPIFFS` (Arduino)
- ESP-IDF equivalent: `esp_uart`, `esp_wifi`, `esp_spiffs_vfs`

---

## 2. MAIN MODULES & RESPONSIBILITIES

### 2.1 Module Structure Overview

```
TinyBMS/
├── src/
│   ├── main.ino                      # Entry point, mutex initialization
│   ├── system_init.cpp               # Boot sequence, WiFi, task creation
│   ├── bridge_uart.cpp               # UART polling (10Hz), Modbus RTU
│   ├── bridge_can.cpp                # CAN transmission (1Hz), PGN encoding
│   ├── bridge_cvl.cpp                # CVL task (20s), state machine
│   ├── bridge_core.cpp               # Bridge initialization, stats
│   ├── bridge_keepalive.cpp          # Victron keep-alive detection (0x305)
│   ├── bridge_event_sink.cpp         # EventBus adapter
│   ├── config_manager.cpp            # JSON config (SPIFFS), hot-reload
│   ├── cvl_logic.cpp                 # Pure CVL algorithm (state machine)
│   ├── logger.cpp                    # Logging (Serial + SPIFFS)
│   ├── watchdog_manager.cpp          # Task WDT management
│   ├── json_builders.cpp             # JSON serialization
│   ├── websocket_handlers.cpp        # WebSocket broadcast
│   ├── web_routes_api.cpp            # REST API (/api/*)
│   ├── web_routes_tinybms.cpp        # Web UI static files
│   ├── web_server_setup.cpp          # AsyncWebServer init
│   ├── victron_alarm_utils.cpp       # Alarm calculation helpers
│   ├── tinybms_config_editor.cpp     # TinyBMS register editor
│   ├── event/
│   │   ├── event_bus_v2.cpp          # EventBus singleton
│   │   └── event_subscriber.cpp      # Subscriber management
│   ├── hal/
│   │   ├── hal_manager.cpp           # HAL singleton
│   │   ├── hal_factory.cpp           # Factory pattern
│   │   ├── esp32/
│   │   │   ├── esp32_uart.cpp        # UART HAL implementation
│   │   │   ├── esp32_can.cpp         # CAN HAL implementation
│   │   │   ├── esp32_storage.cpp     # SPIFFS HAL implementation
│   │   │   ├── esp32_gpio.cpp        # GPIO HAL implementation
│   │   │   ├── esp32_timer.cpp       # Timer HAL implementation
│   │   │   ├── esp32_watchdog.cpp    # Watchdog HAL implementation
│   │   │   └── esp32_factory.cpp     # ESP32 factory implementation
│   │   └── mock/
│   │       └── mock_*.cpp            # Mock implementations for testing
│   ├── mqtt/
│   │   └── victron_mqtt_bridge.cpp   # MQTT publisher (optional)
│   ├── uart/
│   │   └── tinybms_decoder.cpp       # Modbus RTU decoder
│   ├── mappings/
│   │   ├── tiny_read_mapping.cpp     # TinyBMS register catalog
│   │   └── victron_can_mapping.cpp   # Victron PGN mapping
│   └── optimization/
│       ├── adaptive_polling.cpp      # Adaptive polling algorithm
│       └── ring_buffer.cpp           # Ring buffer for diagnostics
├── include/
│   ├── *.h                           # All headers (45+ files)
│   └── (Same structure as src/)
└── data/
    ├── config.json                   # Configuration file
    ├── index.html                    # Web UI
    ├── *.js                          # JavaScript modules
    └── *.json                        # Register catalogs
```

### 2.2 Core Module Responsibilities

#### **Module: Main (main.ino)**
- **Purpose:** Entry point, resource initialization
- **Responsibilities:**
  - Create 4 FreeRTOS mutexes (uart, feed, config, stats)
  - Initialize HAL factory (ESP32Factory)
  - Load configuration from SPIFFS
  - Initialize logger and watchdog
  - Delegate to `initializeSystem()`
- **Code Size:** 119 lines
- **Dependencies:** Arduino, FreeRTOS, Logger, ConfigManager, HAL

#### **Module: System Initialization (system_init.cpp)**
- **Purpose:** Boot sequence orchestration
- **Responsibilities:**
  - Load & configure WiFi (STA/AP fallback)
  - Initialize SPIFFS filesystem
  - Setup EventBus singleton
  - Create FreeRTOS tasks:
    - UART task (10Hz, HIGH_PRIORITY)
    - CAN task (1Hz, HIGH_PRIORITY)
    - CVL task (20s, NORMAL_PRIORITY)
    - WebSocket task (1Hz, NORMAL_PRIORITY)
    - Watchdog task (2Hz, NORMAL_PRIORITY)
    - Web server task (async, NORMAL_PRIORITY)
    - MQTT task (optional, NORMAL_PRIORITY)
  - Configure HAL based on config.json
- **Code Size:** 500+ lines
- **Dependencies:** All modules (central orchestrator)

#### **Module: UART Bridge (bridge_uart.cpp)**
- **Purpose:** TinyBMS data acquisition via Modbus RTU
- **Responsibilities:**
  - Read 6 blocks of TinyBMS registers at 10Hz
  - Parse Modbus RTU responses
  - Calculate SOC, SOH, cell voltages
  - Handle retry logic (configurable, default 3x)
  - Publish `LiveDataUpdate` events to EventBus
  - Publish `MqttRegisterValue` for each register
  - Publish `AlarmRaised`/`AlarmCleared` events
  - Track statistics: polls, errors, timeouts, CRC errors, latency
  - Adaptive polling control
- **Code Size:** 600+ lines
- **Registers Read:**
  - Block 0x32-0x38: Basic data (voltage, current, SOC, temps)
  - Block 0x66-0x67: Min/max cell voltage
  - Block 0x71-0x72: Cell balance bits, status
  - Block 0x131-0x132: Additional sensors
  - Block 0x13B-0x13F: Extended data
  - Block 0x1F4-0x1F9: Alarm/status info
- **Update Frequency:** 10Hz (configurable 100ms)
- **Hardware:** UART via HAL (pins 16 RX, 17 TX)

#### **Module: CAN Bridge (bridge_can.cpp)**
- **Purpose:** Victron CAN-BMS protocol transmission
- **Responsibilities:**
  - Construct 9 Victron PGNs (0x351, 0x355, 0x356, 0x35A, 0x35E, 0x35F, 0x371, 0x378, 0x379, 0x382)
  - Apply Victron scaling and encoding
  - Apply alarm bits (bitfield 0x35A)
  - Handle CAN transmission errors
  - Monitor keep-alive (300ms timeout)
  - Track statistics: tx_count, tx_errors, keepalive_ok
- **Code Size:** 800+ lines
- **Update Frequency:** 1Hz (configurable)
- **Hardware:** CAN via HAL (pins 4 RX, 5 TX, 500kbps bitrate)
- **PGN Details:**
  - 0x351: Pack voltage, current, SOC
  - 0x355: Min/max cell voltage
  - 0x356: Cell temperatures
  - 0x35A: Alarm flags (8 bits)
  - 0x35E: Charge/discharge limits (CCL/DCL)
  - 0x35F: Power request
  - Others: Extended data, timing info

#### **Module: CVL Algorithm (cvl_logic.cpp + bridge_cvl.cpp)**
- **Purpose:** Charge Voltage Limit calculation (6-state machine)
- **Responsibilities:**
  - Compute CVL voltage based on SOC state
  - Compute CCL (charge current limit)
  - Compute DCL (discharge current limit)
  - Handle cell voltage protection (dynamic reduction if any cell >3.65V)
  - State transitions: BULK → TRANSITION → FLOAT_APPROACH → FLOAT → IMBALANCE_HOLD → SUSTAIN
  - Publish `CVLStateChanged` events
- **States:**
  1. **BULK:** SOC < 80%, full power charging
  2. **TRANSITION:** 80% < SOC < 85%, tapering
  3. **FLOAT_APPROACH:** 85% < SOC < 95%, reducing voltage
  4. **FLOAT:** SOC > 95%, constant voltage mode
  5. **IMBALANCE_HOLD:** Imbalance detected, hold voltage
  6. **SUSTAIN:** Very low SOC, recovery mode
- **Code Size:** 400+ lines (logic), 200+ lines (task)
- **Update Frequency:** 20s (configurable)
- **Parameters:** 30+ configurable thresholds (see config_manager.h)

#### **Module: Configuration Manager (config_manager.cpp)**
- **Purpose:** JSON-based configuration persistence
- **Responsibilities:**
  - Load/save `/config.json` from SPIFFS
  - Parse 9 configuration sections (WiFi, Hardware, CVL, MQTT, etc.)
  - Support hot-reload (POST /api/settings)
  - Notify subscribers of changes via `ConfigChanged` events
  - Provide thread-safe access (configMutex)
- **Code Size:** 700+ lines
- **Sections:**
  - **wifi:** STA/AP credentials, hostname
  - **hardware:** UART pins, CAN pins, bitrates
  - **tinybms:** polling intervals, retry counts
  - **victron:** PGN intervals, safety thresholds
  - **cvl:** state thresholds, current limits, cell protection
  - **mqtt:** broker URL, credentials, topics
  - **web_server:** port, WebSocket settings
  - **logging:** log levels, output targets
  - **advanced:** SPIFFS, OTA, watchdog, stack size

#### **Module: EventBus V2 (event_bus_v2.cpp)**
- **Purpose:** Publish-subscribe event distribution
- **Responsibilities:**
  - Type-safe event publishing with metadata
  - Per-type caching (latest event)
  - Subscriber management
  - Thread-safe publication (using std::mutex)
  - Automatic metadata injection (timestamp, sequence, source)
  - Statistics tracking
- **Code Size:** 150+ lines
- **Event Types:** 15+ (LiveDataUpdate, CVLStateChanged, AlarmRaised, ConfigChanged, etc.)
- **Performance:** Lock-free reads via `getLatest()` (reads from cache)
- **Queue:** FreeRTOS queue (32 slots) for async dispatch

#### **Module: HAL (Hardware Abstraction Layer)**
- **Purpose:** Abstract hardware interfaces
- **Responsibilities:**
  - Define interfaces (IHalUart, IHalCan, IHalStorage, IHalGpio, IHalTimer, IHalWatchdog)
  - Provide ESP32 implementations
  - Provide Mock implementations (for testing)
  - Centralize via HalManager singleton
- **Code Size:** 1000+ lines (interfaces + implementations)
- **Interfaces:**
  - **IHalUart:** read/write, timeout control
  - **IHalCan:** transmit, receive, filter config
  - **IHalStorage:** file I/O (SPIFFS/NVS)
  - **IHalGpio:** pin control
  - **IHalTimer:** one-shot/periodic timers
  - **IHalWatchdog:** configuration, feeding, stats
- **Pattern:** Factory + Singleton

#### **Module: Logger (logger.cpp)**
- **Purpose:** Centralized logging with persistence
- **Responsibilities:**
  - Log to Serial (115200 baud)
  - Log to SPIFFS (rotation, max 64KB)
  - Support 4 log levels (ERROR, WARNING, INFO, DEBUG)
  - API endpoint `/api/logs` (download, paginate)
  - DELETE `/api/logs` (purge)
- **Code Size:** 200+ lines
- **Memory Buffer:** Ring buffer for diagnostics

#### **Module: Web Routes & WebSocket (web_routes_api.cpp, websocket_handlers.cpp)**
- **Purpose:** HTTP REST API & real-time WebSocket
- **Responsibilities:**
  - REST endpoints:
    - GET `/api/status` - Complete system state (2-3KB JSON)
    - GET/POST `/api/settings` - Configuration
    - GET/DELETE `/api/logs` - Logging
    - GET `/api/diagnostics` - Heap, stack, watchdog, EventBus stats
    - POST `/api/tinybms/write` - TinyBMS register write
  - WebSocket `/ws` - 1Hz JSON broadcast (1.5KB payload)
  - CORS support
  - Max 4 concurrent WebSocket clients
- **Code Size:** 1200+ lines
- **JSON Library:** ArduinoJson (v6.21.0)

#### **Module: MQTT Bridge (mqtt/victron_mqtt_bridge.cpp)**
- **Purpose:** MQTT publish integration (optional)
- **Responsibilities:**
  - Subscribe to EventBus events (LiveDataUpdate, AlarmRaised)
  - Publish to MQTT broker
  - Support custom root topic
  - Connection/reconnection logic
  - QoS and retain flags
- **Code Size:** 600+ lines
- **Topics:** `{root_topic}/*/register/*`, `{root_topic}/alarms/*`
- **Framework:** ESP32 MQTT client (esp_mqtt_client)

#### **Module: Watchdog Manager (watchdog_manager.cpp)**
- **Purpose:** Hardware watchdog protection
- **Responsibilities:**
  - Configure ESP32 Task WDT (30s timeout)
  - Track feed intervals per task
  - Provide statistics
  - Feed watchdog from all critical tasks
- **Code Size:** 200+ lines
- **Statistics:** feed_count, min_interval, max_interval, average_interval

---

## 3. HARDWARE DEPENDENCIES

### 3.1 ESP32 Peripherals Used

#### **UART (Serial Communication)**
- **Purpose:** TinyBMS Modbus RTU communication
- **Pins:** GPIO16 (RX), GPIO17 (TX) - configurable
- **Baudrate:** 19200 baud (standard Modbus)
- **Protocol:** Modbus RTU (CRC-16)
- **Update Frequency:** 10Hz polling
- **Implementation:** `src/hal/esp32/esp32_uart.cpp` (using Arduino Serial)
- **Timeout:** 100ms (configurable)

#### **CAN Bus**
- **Purpose:** Victron VE.Can protocol
- **Pins:** GPIO4 (TX), GPIO5 (RX) - configurable
- **Bitrate:** 500 kbps (Victron standard)
- **Transceiver:** SN65HVD230 or MCP2551 recommended
- **Implementation:** `src/hal/esp32/esp32_can.cpp` (using twai/CAN module)
- **Termination:** 120Ω resistors on CAN bus (configurable)

#### **SPIFFS Filesystem**
- **Purpose:** Configuration storage, logging, web UI
- **Size:** 1 MB (custom partition)
- **Files:**
  - `/config.json` - Main configuration
  - `/logs.txt` - Rotating log file (64KB)
  - Web UI: `index.html`, `*.js`, `*.css`
  - Register catalogs: `tiny_read.json`, `victron_*_mapping.json`
- **Implementation:** `src/hal/esp32/esp32_storage.cpp`

#### **WiFi**
- **Purpose:** Network connectivity (STA + AP fallback)
- **Modes:**
  - Station (STA): Connect to home WiFi
  - Access Point (AP): Fallback if STA unavailable
- **Implementation:** Arduino WiFi library (ESP32 native)
- **Used by:** Web server, MQTT (optional)

#### **FreeRTOS**
- **Purpose:** Multi-tasking OS kernel
- **Used for:** Task scheduling, mutexes, queues, timers
- **Integrated:** Arduino wrapper around ESP-IDF FreeRTOS
- **Tasks:**
  - UART task (Core 1, HIGH_PRIORITY)
  - CAN task (Core 1, HIGH_PRIORITY)
  - CVL task (Core 0, NORMAL_PRIORITY)
  - WebSocket task (Core 0, NORMAL_PRIORITY)
  - Web server task (Core 0, NORMAL_PRIORITY)

#### **GPIO**
- **Purpose:** General-purpose I/O
- **Used for:** LED indicators (optional), button inputs (future)
- **Implementation:** `src/hal/esp32/esp32_gpio.cpp` (interface defined, minimally used)

### 3.2 External Hardware Components

| Component | Purpose | Connection |
|-----------|---------|-----------|
| TinyBMS Module | Battery management system | UART (GPIO 16/17) |
| CAN Transceiver | CAN protocol translation | SPI-like (GPIO 4/5) |
| Victron GX Device | Energy management system | CAN bus |
| WiFi Router | Network connectivity | Over-the-air |
| MQTT Broker | Message queue (optional) | Ethernet/WiFi |

---

## 4. THIRD-PARTY LIBRARIES

### 4.1 Explicit Dependencies (platformio.ini)

```
lib_deps =
    bblanchon/ArduinoJson@^6.21.0       # JSON parsing/serialization
    me-no-dev/ESPAsyncWebServer@^1.2.3  # Non-blocking HTTP/WebSocket server
    me-no-dev/AsyncTCP@^1.1.1           # Async TCP stack
    sandeepmistry/CAN@^0.3.1            # CAN bus driver
```

### 4.2 Implicit Dependencies (via Arduino/ESP-IDF)

**Core:**
- Arduino (ESP32 variant)
- ESP-IDF (FreeRTOS, peripheral drivers)
- C++ Standard Library (std::vector, std::mutex, std::functional)

**Peripheral Drivers:**
- UART driver (ESP32 native)
- CAN driver (ESP32 TWAI native)
- SPIFFS driver (ESP32 native)
- WiFi stack (ESP32 native)

**System Libraries:**
- std::chrono (timing)
- std::atomic (lock-free operations)
- std::optional (optional values)
- std::vector, std::array (containers)

### 4.3 Library Usage Details

#### **ArduinoJson (v6.21.0)**
- **Purpose:** JSON parsing & serialization
- **Used for:**
  - Loading `config.json` from SPIFFS
  - Serializing API responses (`/api/status`, `/api/settings`)
  - Building WebSocket JSON payloads
- **Key Functions:**
  - `StaticJsonDocument` (fixed-size)
  - `deserializeJson()` (parsing)
  - `serializeJson()` (stringification)
- **Size Impact:** ~30KB binary

#### **ESPAsyncWebServer (v1.2.3)**
- **Purpose:** Non-blocking HTTP/WebSocket server
- **Used for:**
  - Serving REST API endpoints
  - Broadcasting WebSocket messages
  - Static file serving (web UI)
  - CORS support
- **Key Classes:**
  - `AsyncWebServer` (main server)
  - `AsyncWebSocket` (WebSocket endpoint)
  - `AsyncWebSocketClient` (per-client handler)
- **Size Impact:** ~50KB binary
- **⚠️ ESP-IDF Migration Note:** Arduino-specific, would need replacement with `esp_http_server`

#### **AsyncTCP (v1.1.1)**
- **Purpose:** Non-blocking TCP stack
- **Used by:** ESPAsyncWebServer
- **Size Impact:** ~20KB binary
- **⚠️ ESP-IDF Migration Note:** ESP-IDF has native async TCP via `esp_http_server`

#### **CAN (v0.3.1)**
- **Purpose:** CAN bus communication
- **Used for:**
  - Initializing CAN interface
  - Sending Victron PGNs
  - Error handling & statistics
- **Key Functions:**
  - `CAN.begin()` (initialization)
  - `CAN.write()` (frame transmission)
- **Size Impact:** ~10KB binary
- **⚠️ ESP-IDF Migration Note:** Arduino wrapper, ESP-IDF has native `twai` driver

### 4.4 Dependency Analysis for ESP-IDF Migration

| Library | Type | Migratability | Alternative |
|---------|------|---------------|-------------|
| ArduinoJson | Core | **Easy** ✅ | Already cross-platform |
| ESPAsyncWebServer | Framework | **Hard** ⚠️ | `esp_http_server` + custom WebSocket |
| AsyncTCP | Framework | **Hard** ⚠️ | Part of `esp_http_server` |
| CAN | Driver | **Medium** ⚠️ | `twai_driver` (native ESP-IDF) |

---

## 5. CONFIGURATION MANAGEMENT

### 5.1 Configuration File Structure

**Location:** `/data/config.json` (SPIFFS)

**Sections:**
1. **WiFi Configuration**
   - `mode`: "station" (STA/AP)
   - `sta_ssid`, `sta_password`: Home network
   - `sta_hostname`: mDNS hostname
   - `ap_fallback.enabled`: Fallback access point
   - `ap_fallback.ssid`, `password`: AP credentials

2. **Hardware Configuration**
   - **UART:** `rx_pin`, `tx_pin`, `baudrate` (19200), `timeout_ms`
   - **CAN:** `tx_pin`, `rx_pin`, `bitrate` (500000), `termination`, `mode`

3. **TinyBMS Configuration**
   - `poll_interval_ms`: 100ms (default)
   - `uart_retry_count`: 3 retries
   - `uart_retry_delay_ms`: 50ms between retries
   - Adaptive polling: min/max intervals, backoff/recovery steps

4. **Victron Configuration**
   - **Intervals:**
     - `pgn_update_interval_ms`: 1000 (1Hz CAN)
     - `cvl_update_interval_ms`: 20000 (20s CVL)
     - `keepalive_interval_ms`: 1000 (keepalive check)
   - **Thresholds:**
     - Voltage: `undervoltage_v` (44V), `overvoltage_v` (58.4V)
     - Temperature: `overtemp_c` (55°C), `low_temp_charge_c` (0°C)
     - Imbalance: `imbalance_warn_mv` (100), `imbalance_alarm_mv` (200)
     - SOC: `soc_low_percent` (10%), `soc_high_percent` (99%)

5. **CVL Algorithm Configuration**
   - **State Thresholds:** bulk_soc, transition_soc, float_soc, float_exit_soc
   - **Voltage Offsets:** float_approach_offset_mv, float_offset_mv
   - **Cell Protection:** cell_max_voltage_v, cell_safety_threshold_v
   - **Current Limits:** minimum_ccl_in_float_a, dynamic_current_nominal_a
   - 30+ parameters total

6. **MQTT Configuration** (optional)
   - `enabled`: true/false
   - `uri`: broker URL (mqtt://127.0.0.1)
   - `port`: 1883
   - `client_id`, `username`, `password`: credentials
   - `root_topic`: publish prefix
   - `clean_session`, `keepalive_seconds`, `reconnect_interval_ms`
   - `use_tls`, `server_certificate`: security
   - `default_qos`, `retain_by_default`: message options

7. **Web Server Configuration**
   - `port`: 80
   - `websocket_update_interval_ms`: 1000
   - `websocket_min_interval_ms`: 100
   - `websocket_max_payload_bytes`: 4096
   - `max_ws_clients`: 4

8. **Logging Configuration**
   - `serial_baudrate`: 115200
   - `log_level`: INFO, DEBUG, WARNING, ERROR
   - `log_uart_traffic`: false
   - `log_can_traffic`: false
   - `output_serial`, `output_web`: true/true

9. **Advanced Configuration**
   - `enable_spiffs`: true
   - `enable_ota`: false
   - `watchdog_timeout_s`: 30 (seconds)
   - `stack_size_bytes`: 8192

### 5.2 Configuration Loading & Persistence

```cpp
// ConfigManager::begin() - Load from SPIFFS
bool ConfigManager::begin(const char* filename = "/config.json")
  ├─ Load file via HAL::IHalStorage
  ├─ Parse JSON with ArduinoJson
  ├─ Apply defaults for missing values
  └─ Return success/failure

// ConfigManager::save() - Persist to SPIFFS
bool ConfigManager::save()
  ├─ Build JSON document from members
  ├─ Write to HAL::IHalStorage
  └─ Notify subscribers via ConfigChanged event

// Hot-reload via REST API
POST /api/settings {partial JSON}
  ├─ Update relevant struct members
  ├─ Call ConfigManager::save()
  ├─ Publish ConfigChanged event
  └─ Applied immediately (no reboot)
```

### 5.3 Configuration Access Patterns

**Thread Safety:**
- All configuration reads protected by `configMutex` (100ms timeout)
- ConfigManager provides const reference access
- Timeouts exist but inconsistently applied (25ms vs 100ms) ⚠️

**Access Patterns:**
```cpp
// Safe read
if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100))) {
    const auto& cfg = config.cvl;  // Read only
    xSemaphoreGive(configMutex);
}

// Safe write (from web API)
if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100))) {
    config.hardware.uart.baudrate = new_value;
    config.save();
    xSemaphoreGive(configMutex);
}
```

### 5.4 Configuration Validation

- Boundaries checked for numeric fields (min/max)
- String lengths validated
- Enums parsed with fallback to defaults
- Invalid JSON reverts to defaults (logged as warning)

---

## 6. CURRENT ARCHITECTURE PATTERNS

### 6.1 Design Patterns Used

#### **1. Singleton Pattern**
```cpp
// EventBus V2
EventBusV2& eventBus = EventBusV2::getInstance();

// HAL Manager
hal::HalManager::instance().uart().write(...);

// ConfigManager
extern ConfigManager config;  // Global singleton
```

#### **2. Factory Pattern**
```cpp
// HAL Factory
hal::HalFactory* factory = hal::createEsp32Factory();
hal::setFactory(std::make_unique<hal::Esp32Factory>());

// Creation
auto uart = hal::HalManager::instance().uart();  // Created via factory
```

#### **3. Observer Pattern (Publish-Subscribe)**
```cpp
// Publish event
eventBus.publish(LiveDataUpdate{...});

// Subscribe to events
eventBus.subscribe<LiveDataUpdate>([](const auto& event) {
    // React to update
});
```

#### **4. Adapter Pattern**
```cpp
// BridgeEventSink adapts EventBus to bridge usage
class BridgeEventSink {
    void post(const Event& e) { eventBus.publish(e); }
};
```

#### **5. Template Method**
```cpp
// HAL interfaces define template for implementations
class IHalUart {
    virtual Status initialize(const UartConfig& config) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) = 0;
};
```

#### **6. Command Pattern**
```cpp
// ConfigChanged event encapsulates config update
struct ConfigChangeEvent {
    char config_path[64];
    char old_value[32];
    char new_value[32];
};
```

#### **7. Strategy Pattern**
```cpp
// Adaptive polling strategy
if (errors > threshold) {
    interval = std::min(interval + backoff_step, max_interval);
} else if (errors == 0 && interval > min_interval) {
    interval = std::max(interval - recovery_step, min_interval);
}
```

### 6.2 Concurrency Model

**FreeRTOS-based Multi-tasking:**

```
Core 0                          Core 1
├─ WiFi/BT stack               ├─ UART task (10Hz, HIGH_PRIORITY)
├─ CVL task (20s)              ├─ CAN task (1Hz, HIGH_PRIORITY)
├─ WebSocket task (1Hz)        └─ (Other system tasks)
├─ Web server task
└─ MQTT task (optional)
```

**Synchronization Primitives:**

| Mutex | Scope | Timeout | Protects |
|-------|-------|---------|----------|
| `uartMutex` | Global | 100ms | Serial UART access |
| `configMutex` | Global | 25-100ms ⚠️ | Configuration struct |
| `feedMutex` | Global | 100ms | Watchdog feeding |
| `statsMutex` | Global | 10ms | Bridge statistics |
| Internal bus mutex | EventBus | - | Event cache & subscribers |
| Internal data mutex | Bridge | 50ms | Live data access |

**Contention Points:**
- **configMutex:** High (all tasks read config during operation)
- **uartMutex:** Medium (UART + config editor access)
- **feedMutex:** Low (watchdog feeding, 2Hz)
- **statsMutex:** Low (stats updates, 10Hz aggregation)

### 6.3 Data Flow Architecture

```
┌──────────────┐
│   TinyBMS    │
│   (UART)     │
└──────┬───────┘
       │
       ▼
┌─────────────────────────────────────────┐
│   UART Task (10Hz)                      │
│   ├─ Read TinyBMS registers             │
│   ├─ Parse Modbus RTU                   │
│   ├─ Calculate SOC/SOH/cell voltages   │
│   └─ Publish LiveDataUpdate             │
└────────┬────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────┐
│   Event Bus V2 (PUBLISH/SUBSCRIBE)      │
│   ├─ Cache latest LiveDataUpdate        │
│   ├─ Notify subscribers                 │
│   └─ Dispatch to queue                  │
└────────┬────────────────────────────────┘
         │
    ┌────┴────┬────────┬──────────┐
    │          │        │          │
    ▼          ▼        ▼          ▼
  CAN       WebSocket MQTT      Logger
  Task      Task      Task       Task
  │         │        │          │
  │         │        │          ▼
  │         │        │        SPIFFS
  │         │        │        (logs.txt)
  │         │        │
  │         │        ▼
  │         │      MQTT Broker
  │         │
  │         ▼
  │      Browser
  │      (WebSocket)
  │
  ▼
Victron GX
(CAN Bus)
```

### 6.4 Error Handling Strategy

**UART Errors:**
- Retry logic: 3 attempts with configurable delays
- CRC validation: Discard frames with bad CRC
- Timeout handling: Skip poll if UART unresponsive
- Adaptive backoff: Increase polling interval if errors persist

**CAN Errors:**
- TX error tracking: Count tx_errors
- Keep-alive monitoring: Detect if Victron offline (300ms timeout)
- Status event publication: Report CAN failures to EventBus

**Configuration Errors:**
- JSON parse failure: Revert to defaults + log warning
- Invalid thresholds: Clamp to safe boundaries
- Missing fields: Apply hardcoded defaults

**Memory/Resource Errors:**
- EventBus queue overrun: Track queue_overruns, drop events
- Heap exhaustion: Log error, reduce verbosity
- Stack overflow: Task watchdog detects and reboots
- Mutex timeout: Fallback gracefully (skip critical section)

### 6.5 Module Coupling Analysis

**High Coupling (Necessary):**
- `bridge_uart.cpp` ↔ `event_bus_v2.h` (publishes LiveDataUpdate)
- `bridge_can.cpp` ↔ `event_bus_v2.h` (reads LiveData cache)
- `system_init.cpp` ↔ ALL modules (orchestrator)

**Loose Coupling (Good):**
- `bridge_uart.cpp` ↔ `bridge_can.cpp` (via EventBus, no direct calls)
- `logger.cpp` ↔ other modules (uses global logger instance)
- `mqtt/` ↔ other modules (subscribes to EventBus)

**Abstraction Boundaries:**
- HAL interfaces decouple from ESP32 specifics
- ConfigManager decouples from SPIFFS details
- EventBus decouples event producers from consumers

---

## 7. ARCHITECTURAL STRENGTHS & WEAKNESSES

### ✅ Strengths

1. **Event-Driven Architecture**
   - Decoupled modules
   - Easy to extend (add new subscribers)
   - Clean separation of concerns

2. **Hardware Abstraction Layer (HAL)**
   - Multi-platform support (ESP32 + Mock)
   - Testable without hardware
   - Easy to port to new platforms

3. **Configuration-Driven Design**
   - Hot-reload support (no reboot)
   - 9 configuration sections
   - SPIFFS persistence

4. **Thread-Safe Implementation**
   - Mutexes protect critical sections
   - FreeRTOS integration
   - Ring buffers for diagnostics

5. **Comprehensive Documentation**
   - 18+ markdown files
   - Architecture diagrams
   - Module reviews
   - API documentation

6. **Rich Logging & Diagnostics**
   - Serial + SPIFFS logging
   - REST API diagnostics endpoint
   - Watchdog statistics
   - EventBus statistics

### ⚠️ Weaknesses & Technical Debt

1. **configMutex Inconsistent Timeouts** (Low Impact)
   - Some uses: 25ms
   - Others: 100ms
   - Recommendation: Standardize to 100ms

2. **Arduino Framework Dependency** (For ESP-IDF migration)
   - AsyncWebServer is Arduino-specific
   - AsyncTCP is Arduino-specific
   - Direct ESP32 peripheral access cleaner for production

3. **Double Source of Truth**
   - `bridge.live_data_` struct + EventBus cache
   - Currently synchronized but maintenance burden
   - Could consolidate (cache as single source)

4. **Stats Not Fully Protected** (Very Low Impact)
   - UART stats (_retry_count, _timeouts) lack mutex
   - Occasional tearing possible (negligible)
   - statsMutex only 10ms timeout

5. **WebSocket Limits Untested**
   - Max 4 clients configured
   - No load testing beyond 4 clients
   - Memory growth if exceeded

6. **Mock HAL Not Exercised in CI** (Low Impact)
   - Mock implementations defined but not systematically tested
   - Only native tests run (C++)
   - No Python end-to-end tests against mocks

---

## 8. SUMMARY TABLE: ESP-IDF MIGRATION IMPLICATIONS

| Component | Current | ESP-IDF Native | Effort | Risk |
|-----------|---------|-----------------|--------|------|
| **UART** | Arduino Serial | esp_uart driver | Low | Low |
| **CAN** | Arduino CAN lib | twai_driver | Low | Low |
| **SPIFFS** | Arduino wrapper | esp_spiffs_vfs | Low | Low |
| **WiFi** | Arduino WiFi | esp_wifi | Medium | Medium |
| **WebServer** | ESPAsyncWebServer | esp_http_server | **High** | **Medium** |
| **WebSocket** | AsyncWebServer WS | Custom implementation | **High** | **High** |
| **FreeRTOS** | Via Arduino | Direct ESP-IDF | Low | Low |
| **Build System** | PlatformIO | ESP-IDF CMake | High | High |
| **Logging** | Arduino Serial | esp_log | Low | Low |
| **JSON** | ArduinoJson | Keep as-is | None | None |

---

## 9. CRITICAL FINDINGS FOR ESP-IDF MIGRATION

### 9.1 Must-Do Changes
1. Replace `ESPAsyncWebServer` with `esp_http_server`
2. Implement custom WebSocket handler for `esp_http_server`
3. Replace Arduino `Serial` with `esp_uart_driver` or keep via Arduino IDF variant
4. Update partition table to ESP-IDF format (CMakeLists.txt)
5. Create CMakeLists.txt build files for esp-idf components
6. Replace Arduino `WiFi` class with `esp_wifi` API (or keep through component)

### 9.2 Nice-to-Have Optimizations
1. Use native FreeRTOS directly (remove Arduino wrapper)
2. Use `lwip` DNS/mDNS instead of Arduino abstractions
3. Profile and optimize WebSocket latency
4. Enable flash encryption for config.json

### 9.3 Recommended Approach (Phased)
**Phase 1:** Keep Arduino layer, migrate build to ESP-IDF (CMakeLists.txt wrapper)
**Phase 2:** Replace critical components (WebServer) one at a time
**Phase 3:** Full native ESP-IDF (optional, not required for production)

---

## 10. DELIVERABLES & NEXT STEPS

**Current Status:** v2.5.0, Production Ready (9.5/10)
**Build System:** PlatformIO + Arduino framework
**Code Quality:** Excellent (event-driven, well-documented, tested)
**Architecture:** Modular, loosely-coupled, HAL-abstracted

**For ESP-IDF Migration Plan:**
- Review CRITICAL in section 9.1
- Start with phased approach (Phase 1: CMakeLists.txt wrapper)
- Preserve EventBus and configuration system (working well)
- Focus migration effort on peripheral abstraction (HAL implementations)

