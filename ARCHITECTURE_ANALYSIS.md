# TinyBMS-Victron Bridge: Complete Architecture Analysis

## Project Overview

**TinyBMS-Victron Bridge** is an ESP32-based UART-to-CAN gateway that bridges communication between TinyBMS battery management systems and Victron energy management systems. The system implements:

- Real-time UART/Modbus communication with TinyBMS
- CAN-BMS protocol translation to Victron systems
- CVL (Charge Voltage Limit) algorithm for intelligent charging
- FreeRTOS multi-tasking architecture with watchdog protection
- Web-based monitoring and configuration interface
- JSON-based configuration persistence on SPIFFS

**Statistics:**
- 20 source files (.cpp, .h, .ino)
- 2,713 total lines of code
- ~11 KB JSON configurations
- Real-time monitoring via WebSocket

---

## 1. UART Communication Implementation

### 1.1 Hardware Configuration

**File:** `/home/user/TinyBMS/include/tinybms_victron_bridge.h` (Lines 33-40)

```c
#define TINYBMS_UART_RX        16
#define TINYBMS_UART_TX        17
#define TINYBMS_UART_BAUD      115200
```

**Configuration File:** `/home/user/TinyBMS/data/config.json` (Lines 14-18)

```json
"hardware": {
  "uart": {
    "rx_pin": 16,
    "tx_pin": 17,
    "baudrate": 115200,
    "timeout_ms": 1000
  }
}
```

### 1.2 UART Communication Stack

**Primary Implementation:** `/home/user/TinyBMS/src/tinybms_victron_bridge.cpp`

The UART communication layer:

1. **Initialization** (Lines 40-64)
   - Uses HardwareSerial instance: `tiny_uart_{1}`
   - Mutex-protected initialization via `uartMutex`
   - Configurable baud rate from `config.hardware.uart.baudrate`

2. **Register Reading** (Lines 69-89)
   - Function: `readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output)`
   - Currently STUBBED with TODO comment: "Implement actual Modbus RTU read"
   - Provides mutex protection for thread-safe UART access
   - Returns data in 16-bit register format

3. **Supported TinyBMS Registers** (Lines 53-74)

   | Register | Name | Purpose |
   |----------|------|---------|
   | 36 | TINY_REG_VOLTAGE | Battery voltage (float) |
   | 38 | TINY_REG_CURRENT | Charge/discharge current (float) |
   | 40-41 | MIN/MAX_CELL | Min/Max cell voltages (mV) |
   | 45 | TINY_REG_SOH | State of Health (%) |
   | 46 | TINY_REG_SOC | State of Charge (%) |
   | 48 | TEMP_INTERNAL | Internal temperature (°C/10) |
   | 50 | ONLINE_STATUS | Device status (0x91-0x97 = OK) |
   | 52 | BALANCING | Cell balancing bitmask |
   | 102-103 | MAX_DISCHARGE/CHARGE | Current limits (A/10) |
   | 300-320 | CONFIG | Capacity, voltage thresholds, limits |

### 1.3 UART Task (Main Polling Loop)

**File:** `/home/user/TinyBMS/src/tinybms_victron_bridge.cpp` (Lines 94-142)

```
Task: uartTask
Priority: TASK_HIGH_PRIORITY (2)
Stack Size: TASK_DEFAULT_STACK_SIZE (4096 bytes)
Interval: UART_POLL_INTERVAL_MS (100 ms = 10 Hz)
```

**Data Flow:**
```
1. Check timing (every 100ms)
2. Read 17 consecutive registers from TinyBMS
3. Parse received data into TinyBMS_LiveData structure
4. Publish data to liveDataQueue via xQueueOverwrite()
5. Update bridge.live_data_ internal state
6. Log via LOG_LIVEDATA macro
7. Feed watchdog
8. Handle errors and track UART_errors counter
```

**Error Handling:**
- Failed reads increment `stats.uart_errors`
- Sets `data.online_status = false` on failure
- Continues operating with last known data
- Does NOT retry immediately (prevents blocking)

---

## 2. CAN Bus Communication Implementation

### 2.1 Hardware Configuration

**File:** `/home/user/TinyBMS/include/tinybms_victron_bridge.h` (Lines 38-40)

```c
#define VICTRON_CAN_TX         5
#define VICTRON_CAN_RX        4
#define VICTRON_CAN_BITRATE    500000  // 500 kbps
```

**Configuration File:** `/home/user/TinyBMS/data/config.json` (Lines 20-26)

```json
"can": {
  "tx_pin": 5,
  "rx_pin": 4,
  "bitrate": 500000,
  "mode": "normal"
}
```

### 2.2 Victron CAN-BMS Protocol

**PGN IDs (Parameter Group Numbers):**

| PGN | Name | Frequency | Purpose |
|-----|------|-----------|---------|
| 0x351 | CVL/CCL/DCL | 1 Hz (critical) | Charge Voltage/Current/Discharge Current Limits |
| 0x355 | SOC/SOH | 1 Hz | State of Charge / State of Health |
| 0x356 | VOLTAGE_CURRENT | 1 Hz | Battery voltage, current, temperature |
| 0x35A | ALARMS | Event-driven | Fault conditions and warnings |
| 0x35E | MANUFACTURER | 1 Hz | Manufacturer information |
| 0x35F | BATTERY_INFO | 1 Hz | Cell count, capacity info |
| 0x370-0x371 | NAME | 1 Hz | Battery name (2 messages) |
| 0x378 | ENERGY | 1 Hz | Energy counters |
| 0x379 | CAPACITY | 1 Hz | Battery capacity |

**File:** `/home/user/TinyBMS/data/tinybms_victron_mapping.json`

### 2.3 CAN Task (Message Broadcasting)

**File:** `/home/user/TinyBMS/src/tinybms_victron_bridge.cpp` (Lines 147-177)

```
Task: canTask
Priority: TASK_HIGH_PRIORITY (2)
Stack Size: TASK_DEFAULT_STACK_SIZE (4096 bytes)
Interval: PGN_UPDATE_INTERVAL_MS (1000 ms = 1 Hz)
```

**Current Implementation Status:** STUBBED
- Currently only peeks at liveDataQueue
- TODO: "Send CAN PGNs" - Not yet implemented
- Counts transmitted frames in `stats.can_tx_count`
- Logs wireframe transmission

**PGN Building Functions (Declared but not implemented):**
- `buildPGN_0x351()` - CVL/CCL/DCL
- `buildPGN_0x355()` - SOC/SOH  
- `buildPGN_0x356()` - Voltage/Current/Temp
- `buildPGN_0x35A()` - Alarms
- `buildPGN_0x35E()` - Manufacturer
- `buildPGN_0x35F()` - Battery Info

### 2.4 CAN Reception (Victron → TinyBMS)

**Function:** `processVictronRX()` (Declared but not implemented)

Currently no handling of incoming CAN messages. This would be needed for:
- Receiving keep-alive frames
- Handling Victron system status
- Processing any remote commands

---

## 3. Data Flow Architecture

### 3.1 Data Structure: TinyBMS_LiveData

**File:** `/home/user/TinyBMS/include/shared_data.h` (Lines 15-50)

```cpp
struct TinyBMS_LiveData {
    float voltage;               // V
    float current;               // A (negative = discharge)
    uint16_t min_cell_mv;        // mV
    uint16_t max_cell_mv;        // mV
    uint16_t soc_raw;            // Raw SOC (scale 0.002%)
    uint16_t soh_raw;            // Raw SOH (scale 0.002%)
    int16_t temperature;         // 0.1°C
    uint16_t online_status;      // 0x91-0x97 = OK, 0x9B = Fault
    uint16_t balancing_bits;     // Bitfield: active cell balancing
    uint16_t max_discharge_current; // 0.1A
    uint16_t max_charge_current;    // 0.1A
    float soc_percent;           // 0–100%
    float soh_percent;           // 0–100%
    uint16_t cell_imbalance_mv;  // Max - Min cell diff (mV)
};
```

### 3.2 Data Flow: Complete Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│                    UART TASK (High Priority)                    │
│                    (Runs every 100ms)                          │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ├─→ readTinyRegisters() [UART Line 16/17]
                 │
                 ├─→ Parse into TinyBMS_LiveData
                 │
                 ├─→ xQueueOverwrite(liveDataQueue, &data)
                 │       ↓
                 └─→ bridge.live_data_ = data
                      │
        ┌─────────────┼─────────────┬──────────────┐
        │             │             │              │
        ▼             ▼             ▼              ▼
    CAN Task     CVL Task    WebSocket Task   Web API
    (1 Hz)      (20s)        (1 Hz)         Routes
        │             │             │              │
        ├→ PGN 0x355  ├→ Update     ├→ JSON      ├→/api/status
        ├→ PGN 0x356  │   CVL       │ broadcast │→/api/config
        ├→ PGN 0x35A  │   state     │   status  └→/api/logs
        └→ PGN 0x351  └→ log        └→ notify
           (CVL)                      clients


FreeRTOS Synchronization:
┌──────────────────────────────┐
│   liveDataQueue (Size=1)     │ ← Latest data snapshot
├──────────────────────────────┤
│   uartMutex (UART access)    │
├──────────────────────────────┤
│   feedMutex (Watchdog feed)  │
├──────────────────────────────┤
│   configMutex (Config R/W)   │
└──────────────────────────────┘
```

### 3.3 Data Queue Architecture

**File:** `/home/user/TinyBMS/src/main.ino` (Line 92)

```cpp
liveDataQueue = xQueueCreate(1, sizeof(TinyBMS_LiveData));
```

**Configuration:** `/home/user/TinyBMS/include/rtos_config.h` (Line 13)
```c
#define LIVE_DATA_QUEUE_SIZE 1  // Single entry - latest live data only
```

**Access Pattern:**
- **Write:** `uartTask` uses `xQueueOverwrite()` (overwrites old data, never blocks)
- **Read:** `canTask`, `cvlTask`, `websocketTask` use `xQueuePeek()` (non-blocking read)

**Rationale:**
- Queue size = 1 ensures only latest snapshot is accessible
- Prevents stale data from being processed
- Non-blocking peeks prevent task starvation
- Efficient circular buffer with single element

---

## 4. CVL Algorithm (Charge Voltage Limit)

### 4.1 CVL Task Implementation

**File:** `/home/user/TinyBMS/src/tinybms_victron_bridge.cpp` (Lines 182-210)

```
Task: cvlTask
Priority: TASK_NORMAL_PRIORITY (1)
Stack Size: 2048 bytes
Interval: CVL_UPDATE_INTERVAL_MS (20000 ms = 50 mHz)
```

**Current Implementation:** STUB
- Reads latest data from queue
- Updates `stats.cvl_current_v` with current voltage
- Updates `stats.cvl_state` to CVL_BULK_ABSORPTION
- Logs voltage

### 4.2 CVL State Machine

**File:** `/home/user/TinyBMS/include/tinybms_victron_bridge.h` (Lines 121-127)

```cpp
enum CVL_State {
    CVL_BULK_ABSORPTION,   // SOC < 90%
    CVL_TRANSITION,        // 90% <= SOC < 95%
    CVL_FLOAT_APPROACH,    // 95% <= SOC < 100%
    CVL_FLOAT,             // SOC >= 100% or MaxCell >= FullyCharged
    CVL_IMBALANCE_HOLD     // Cell imbalance > threshold
};
```

### 4.3 CVL Configuration

**File:** `/home/user/TinyBMS/data/config.json` (Lines 43-54)

```json
"cvl_algorithm": {
  "enabled": true,
  "bulk_soc_threshold": 90.0,
  "transition_soc_threshold": 95.0,
  "float_soc_threshold": 100.0,
  "float_exit_soc": 88.0,
  "float_approach_offset_mv": 50,
  "float_offset_mv": 100,
  "minimum_ccl_in_float_a": 0.5,
  "imbalance_hold_threshold_mv": 100,
  "imbalance_release_threshold_mv": 30
}
```

**Dynamic CVL Mapping:** `/home/user/TinyBMS/data/tinybms_victron_mapping.json`

| State | Condition | CVL Formula |
|-------|-----------|-------------|
| BULK | SOC < 90% | overvoltage_cutoff × cells ÷ 10 |
| TRANSITION | 90% ≤ SOC < 95% | fully_charged × cells ÷ 10 |
| FLOAT_APPROACH | 95% ≤ SOC < 100% | (fully_charged - 50mV) × cells ÷ 10 |
| FLOAT | SOC ≥ 100% | (fully_charged - 100mV) × cells ÷ 10, CCL min 0.5A |

---

## 5. Web Interface & Data Distribution

### 5.1 Web Server Architecture

**File:** `/home/user/TinyBMS/src/web_server_setup.cpp`

```cpp
AsyncWebServer server(80);        // HTTP on port 80
AsyncWebSocket ws("/ws");         // WebSocket endpoint
```

**Setup Flow:**
1. Register WebSocket handler
2. Serve static files from SPIFFS (index.html, *.js)
3. Register API routes (see below)
4. Add CORS headers if enabled
5. Start server on configured port

### 5.2 REST API Endpoints

**File:** `/home/user/TinyBMS/src/web_routes_api.cpp`

| Method | Endpoint | Purpose | Response |
|--------|----------|---------|----------|
| GET | `/api/status` | Live battery data + stats | JSON |
| GET | `/api/config/system` | System configuration | JSON |
| PUT | `/api/config/system` | Update system config | Status |
| GET | `/api/watchdog` | Watchdog status | JSON |
| PUT | `/api/watchdog` | Control watchdog | Status |
| POST | `/api/reboot` | Reboot system | Status |

**File:** `/home/user/TinyBMS/src/web_routes_tinybms.cpp`

| Method | Endpoint | Purpose | Response |
|--------|----------|---------|----------|
| GET | `/api/config/tinybms` | TinyBMS battery config | JSON |
| PUT | `/api/config/tinybms` | Update TinyBMS config | Status |

### 5.3 Status JSON Structure

**File:** `/home/user/TinyBMS/src/json_builders.cpp` (Lines 30-84)

```json
{
  "live_data": {
    "voltage": 48.50,
    "current": 12.5,
    "soc_percent": 75.0,
    "soh_percent": 95.5,
    "temperature": 25.3,
    "min_cell_mv": 3420,
    "max_cell_mv": 3480,
    "cell_imbalance_mv": 60,
    "balancing_bits": 0,
    "online_status": true
  },
  "stats": {
    "cvl_current_v": 48.50,
    "cvl_state": 0,
    "cvl_state_name": "BULK",
    "can_tx_count": 12345,
    "can_rx_count": 0,
    "uart_errors": 2,
    "victron_keepalive_ok": false
  },
  "watchdog": {
    "enabled": true,
    "timeout_ms": 30000,
    "time_since_last_feed_ms": 1234,
    "feed_count": 567,
    "health_ok": true,
    "last_reset_reason": "POWERON"
  },
  "uptime_ms": 3600000
}
```

### 5.4 WebSocket Real-Time Updates

**File:** `/home/user/TinyBMS/src/websocket_handlers.cpp` (Lines 80-122)

```
Task: websocketTask
Priority: TASK_NORMAL_PRIORITY (1)
Stack Size: TASK_DEFAULT_STACK_SIZE (4096 bytes)
Interval: config.web_server.websocket_update_interval_ms (1000 ms)
```

**Broadcast Data:** Compact JSON with live data only
```json
{
  "voltage": 48.50,
  "current": 12.5,
  "soc_percent": 75.0,
  "soh_percent": 95.5,
  "temperature": 25.3,
  "min_cell_mv": 3420,
  "max_cell_mv": 3480,
  "cell_imbalance_mv": 60,
  "online_status": true,
  "uptime_ms": 3600000
}
```

**WebSocket Event Handling:**
- `WS_EVT_CONNECT` - Client connection logged
- `WS_EVT_DISCONNECT` - Client disconnection logged
- `WS_EVT_DATA` - Data received (currently logged only)
- Broadcast to all connected clients via `notifyClients()`

---

## 6. Memory Access & Storage Patterns

### 6.1 Configuration Management

**File:** `/home/user/TinyBMS/include/config_manager.h`

Configuration is organized in nested structures:

```cpp
struct ConfigManager {
    WiFiConfig wifi;           // WiFi SSID, password, AP fallback
    HardwareConfig hardware;   // UART/CAN pins and bitrates
    TinyBMSConfig tinybms;     // Poll intervals, retry logic
    VictronConfig victron;     // Update intervals, timeouts
    CVLConfig cvl;             // CVL thresholds and parameters
    WebServerConfig web_server; // Port, WebSocket interval
    LoggingConfig logging;     // Log level, UART/CAN traffic flags
    AdvancedConfig advanced;   // Watchdog, stack size, OTA
};
```

### 6.2 Configuration Persistence

**File:** `/home/user/TinyBMS/src/config_manager.cpp`

```
Configuration Flow:
┌─────────────────────────────────────────────────────────┐
│                   SPIFFS Filesystem                      │
│                    /config.json                         │
│                    (4096 bytes)                         │
└────────────┬───────────────────────────────────────────┘
             │
             ├─→ Load: begin(filename)
             │   ├─ Read file from SPIFFS
             │   ├─ Parse JSON (ArduinoJson)
             │   ├─ Populate all config structures
             │   └─ Apply defaults if missing
             │
             ├─→ Access: Protected by configMutex
             │   ├─ Web API can read/write
             │   ├─ Tasks can read (protected)
             │   └─ All access serialized
             │
             └─→ Save: save(filename)
                 ├─ Build JSON from structures
                 ├─ Write to SPIFFS
                 └─ Flush to storage
```

**Mutex Protection:** `configMutex` (FreeRTOS semaphore)
- Acquired before any read/write operation
- 100ms timeout
- Prevents race conditions across tasks

### 6.3 SPIFFS File Organization

**Configuration:** `/home/user/TinyBMS/platformio.ini` (Line 35)

```ini
board_build.filesystem = spiffs
data_dir = data  # Upload: pio run --target uploadfs
```

**Files on SPIFFS:**
- `/config.json` (4 KB) - System configuration
- `/logs.txt` (0-100 KB) - Log file with rotation
- `/index.html` - Web UI main page
- `/*.js`, `/*.css` - Web assets

### 6.4 Logging System

**File:** `/home/user/TinyBMS/include/logger.h` & `/home/user/TinyBMS/src/logger.cpp`

```
Logging Architecture:
┌──────────────────────────────────────────┐
│         Logger Instance                  │
│  (Global extern Logger logger)           │
└────────────────┬─────────────────────────┘
                 │
    ┌────────────┼────────────┐
    │            │            │
    ▼            ▼            ▼
 Serial      SPIFFS        Configurable
(Console)   /logs.txt      Log Level
   │            │              │
   │      Rotation:        ┌────┴──────┐
   │      100 KB max       │           │
   │                    LOG_ERROR   LOG_DEBUG
   │                   LOG_WARNING  LOG_INFO
   │
   └─→ Log Format: [timestamp] [LEVEL] message
```

**Log Levels:**
- `LOG_ERROR` (0) - Critical failures only
- `LOG_WARNING` (1) - Warnings + errors
- `LOG_INFO` (2) - Normal operation (default)
- `LOG_DEBUG` (3) - Detailed debugging

**Log Rotation:**
- File: `/logs.txt` on SPIFFS
- Max size: 100 KB
- Strategy: Delete and recreate when full
- Mutex-protected: `log_mutex_`

---

## 7. FreeRTOS Task Architecture

### 7.1 Task Hierarchy

**File:** `/home/user/TinyBMS/src/main.ino` (Lines 106-112)

```
setup() → Creates 6 FreeRTOS Tasks:

┌─────────────────────────────────────────────────────┐
│  High Priority (2) - Critical Functions             │
├─────────────────────────────────────────────────────┤
│  [UART Task]  100ms  Read TinyBMS registers         │
│  [CAN Task]   1000ms Send Victron CAN frames        │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│  Normal Priority (1) - Service Functions            │
├─────────────────────────────────────────────────────┤
│  [Web Server Task]  Loop    HTTP server             │
│  [WebSocket Task]   1000ms  Broadcast to clients    │
│  [CVL Task]         20000ms Calculate CVL voltage   │
│  [Watchdog Task]    5000ms  Feed hardware WDT       │
└─────────────────────────────────────────────────────┘
```

### 7.2 Task Configuration

**File:** `/home/user/TinyBMS/include/rtos_config.h`

```c
// Queue
#define LIVE_DATA_QUEUE_SIZE 1

// Stack Sizes
#define TASK_DEFAULT_STACK_SIZE 4096

// Priorities
#define TASK_HIGH_PRIORITY 2
#define TASK_NORMAL_PRIORITY 1

// Timing
#define WEBSOCKET_UPDATE_INTERVAL_MS 1000
#define UART_POLL_INTERVAL_MS        100
#define PGN_UPDATE_INTERVAL_MS       1000
#define CVL_UPDATE_INTERVAL_MS       20000
```

### 7.3 Synchronization Primitives

**File:** `/home/user/TinyBMS/src/main.ino` (Lines 17-20)

```cpp
SemaphoreHandle_t uartMutex;     // UART line access
SemaphoreHandle_t feedMutex;     // Watchdog feed protection
SemaphoreHandle_t configMutex;   // Configuration R/W
QueueHandle_t liveDataQueue;     // Live data snapshot
```

**Usage:**
- All UART operations must acquire `uartMutex`
- Watchdog feeding serialized via `feedMutex`
- Configuration access protected by `configMutex`
- Data sharing via single-element queue

---

## 8. Watchdog Management

### 8.1 Watchdog Task

**File:** `/home/user/TinyBMS/src/watchdog_manager.cpp`

```
Task: WatchdogManager::watchdogTask
Priority: TASK_NORMAL_PRIORITY (1)
Stack Size: 2048 bytes
Timeout: Configurable (default 30s, range 1-30s)
```

**Feed Locations:**
1. UART task (every 100ms)
2. CAN task (every 1000ms)
3. CVL task (every 20000ms)
4. WebSocket task (every 1000ms)
5. WiFi init (every 500ms during connect)
6. SPIFFS operations (periodic)
7. Web API requests (after operations)

**Configuration:**
```json
"advanced": {
  "watchdog_timeout_s": 30
}
```

**Web API Control:**
- `GET /api/watchdog` - Read watchdog status
- `PUT /api/watchdog` - Enable/disable, set timeout

---

## 9. Current Implementation Status

### Fully Implemented (Production-Ready):
✓ UART communication initialization  
✓ UART task polling loop  
✓ TinyBMS register definitions  
✓ Data structure definitions  
✓ FreeRTOS task architecture  
✓ Configuration management (JSON)  
✓ Web server & REST API  
✓ WebSocket real-time updates  
✓ Logging system with rotation  
✓ Watchdog management  
✓ WiFi connectivity  
✓ SPIFFS filesystem  

### Partially Implemented (Stubbed):
⚠ UART Modbus RTU reading - Function exists but returns dummy data  
⚠ CAN initialization - Basic structure, no actual CAN bus init  
⚠ CAN message sending - Counts frames but doesn't send  
⚠ CVL algorithm - Task runs but doesn't compute, always returns BULK state  
⚠ CAN reception handling - Function declared but empty  

### Not Implemented:
✗ Actual PGN building (0x351, 0x355, 0x356, etc.)  
✗ CAN keep-alive timeout monitoring  
✗ Error recovery strategies  
✗ Configuration editor UI  

---

## 10. Data Patterns & Best Practices Observed

### 10.1 Thread-Safe Data Sharing

**Pattern:** Mutex-Protected Reads and Writes
```cpp
if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Read/write config safely
    xSemaphoreGive(configMutex);
}
```

**Pattern:** Queue-Based Status Broadcasting
```cpp
// Write: UART task publishes latest data
xQueueOverwrite(liveDataQueue, &data);

// Read: CAN/WebSocket tasks consume non-blocking
xQueuePeek(liveDataQueue, &data, 0);
```

### 10.2 Logging Integration

**Macro-Based Conditional Logging:**
```cpp
#define BRIDGE_LOG(level, msg) logger.log(level, String("[BRIDGE] ") + msg)

// Usage with proper formatting
BRIDGE_LOG(LOG_ERROR, "UART read failed — error: " + String(error_code));
```

**Configuration-Aware Logging:**
```cpp
if (config.logging.log_uart_traffic) {
    logger.log(LOG_DEBUG, "UART RX: " + hex_dump);
}
```

### 10.3 Non-Blocking Task Operations

**Technique:** Time-Based State Machines
```cpp
uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
if (now - last_update_ms >= INTERVAL_MS) {
    // Do work
    last_update_ms = now;
}
vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
```

**Benefit:** Tasks never block, system remains responsive

---

## 11. Key Files Reference

### Core Bridge Logic:
- `/home/user/TinyBMS/include/tinybms_victron_bridge.h` - Header with register definitions
- `/home/user/TinyBMS/src/tinybms_victron_bridge.cpp` - Bridge implementation (UART/CAN/CVL tasks)

### Data Structures:
- `/home/user/TinyBMS/include/shared_data.h` - TinyBMS_LiveData + logging macros
- `/home/user/TinyBMS/include/config_manager.h` - Configuration structures

### Web/API:
- `/home/user/TinyBMS/src/web_server_setup.cpp` - Server initialization
- `/home/user/TinyBMS/src/web_routes_api.cpp` - REST API endpoints
- `/home/user/TinyBMS/src/web_routes_tinybms.cpp` - TinyBMS config endpoints
- `/home/user/TinyBMS/src/json_builders.cpp` - JSON response formatting
- `/home/user/TinyBMS/src/websocket_handlers.cpp` - WebSocket broadcasting

### Configuration:
- `/home/user/TinyBMS/data/config.json` - System configuration
- `/home/user/TinyBMS/data/tinybms_victron_mapping.json` - Protocol mapping reference

### Supporting:
- `/home/user/TinyBMS/src/logger.cpp` - Logging implementation
- `/home/user/TinyBMS/src/watchdog_manager.cpp` - Watchdog control
- `/home/user/TinyBMS/src/system_init.cpp` - System initialization
- `/home/user/TinyBMS/src/main.ino` - Entry point

---

## 12. Known Issues & TODOs

### Critical (Blocking):
1. **Modbus RTU Not Implemented** (tinybms_victron_bridge.cpp:75)
   - UART read returns dummy zeros
   - Needs CRC-16 calculation
   - Needs Modbus protocol parser

2. **CAN Initialization Missing** (tinybms_victron_bridge.cpp:54)
   - CAN bus not configured
   - No actual hardware init

3. **PGN Building Stubs** (tinybms_victron_bridge.cpp:182-187)
   - All PGN builder functions declared but not implemented
   - Cannot send Victron messages

### Medium (Partial):
4. **CVL Algorithm Not Computing** (tinybms_victron_bridge.cpp:189-192)
   - Always returns CVL_BULK_ABSORPTION
   - No actual state transitions
   - No voltage calculations

5. **CAN Reception Not Handled** (tinybms_victron_bridge.cpp:179)
   - No processing of incoming Victron messages
   - No keep-alive timeout checks

### Minor (Enhancement):
6. **Route Function Name Mismatch**
   - web_server_setup.cpp calls setupAPIRoutes() and setupTinyBMSConfigRoutes()
   - But functions are named registerApiRoutes() and registerTinyBMSRoutes()
   - May cause link errors

---

End of Architecture Analysis
