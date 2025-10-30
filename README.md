# TinyBMS ↔ Victron ESP-IDF Bridge

The TinyBMS bridge is a pure ESP-IDF application that reproduces the complete feature set of the historical Arduino firmware while keeping the modern `app_main`/CMake architecture. The firmware ingests TinyBMS telemetry over UART, republishes Victron-compatible CAN frames, exposes a Wi-Fi access point + optional STA connectivity, serves the existing HTML/JS monitoring UI from SPIFFS and provides a REST API for configuration and diagnostics.

## ✨ Highlights

- **ESP-IDF only** – UART, CAN (TWAI), Wi-Fi, SPIFFS and HTTP are handled by native ESP-IDF components without Arduino shims.
- **Full web experience** – the legacy dashboard, monitoring and settings pages are compiled into a SPIFFS image and served through `esp_http_server` with optional CORS and JSON APIs.
- **REST configuration** – `/api/config/system` lets you read/write Wi-Fi, access-point and CORS settings that are stored in NVS and applied live.
- **Real-time status** – `/api/status` returns bridge diagnostics and the most recent TinyBMS sample; the web UI consumes it for live graphs.
- **Deterministic tasks** – dedicated UART, CAN and diagnostics tasks with configurable priorities and queue depth.
- **Victron friendly CAN frames** – packs voltage/current/SOC/temperature into little-endian CAN data frames plus a keepalive PGN.
- **Runtime configurability** – UART/CAN pins, baudrate and diagnostic cadence are exposed in Kconfig (`menuconfig`).

## 🗂️ Repository layout

```
TinyBMS/
├── CMakeLists.txt          # ESP-IDF project entry point
├── main/
│   ├── app_main.cpp        # Application bootstrap (app_main)
│   ├── bridge.cpp          # UART ↔ CAN bridge implementation
│   ├── config.cpp          # Kconfig-backed bridge configuration
│   ├── diagnostics.cpp     # Health counters and periodic logging
│   ├── http_server.cpp     # REST + static file server (`esp_http_server`)
│   ├── system_config.cpp   # Persistent system settings stored in NVS
│   ├── wifi_manager.cpp    # Wi-Fi AP/STA lifecycle helpers
│   └── Kconfig.projbuild   # Custom configuration options
├── include/
│   ├── bridge.hpp
│   ├── config.hpp
│   ├── diagnostics.hpp
│   ├── http_server.hpp
│   ├── system_config.hpp
│   └── wifi_manager.hpp
├── data/                   # Static web assets mounted in SPIFFS
├── legacy/                 # Arduino-based firmware kept for reference only
└── docs/, tests/           # Additional documentation and tests
```

## ⚙️ Building

### Using ESP-IDF tools

```bash
idf.py set-target esp32
idf.py menuconfig       # (optional) adjust pins / baudrate / queue depth
idf.py build
idf.py flash monitor
idf.py spiffs           # build SPIFFS image with /data web assets
idf.py -p /dev/ttyUSB0 spiffs-flash
```

### Using PlatformIO

`platformio.ini` is configured for `framework = espidf`. From the repository root:

```bash
pio run
pio run -t upload
pio run -t uploadfs      # upload SPIFFS image for web UI
```

## 🔧 Configuration knobs (menuconfig)

| Symbol | Default | Purpose |
|--------|---------|---------|
| `TINYBMS_UART_PORT` | `1` | UART controller connected to TinyBMS |
| `TINYBMS_UART_BAUD` | `115200` | UART baudrate |
| `TINYBMS_UART_RX_PIN` / `TINYBMS_UART_TX_PIN` | `16` / `17` | UART pins |
| `TINYBMS_CAN_RX_PIN` / `TINYBMS_CAN_TX_PIN` | `4` / `5` | TWAI pins towards Victron |
| `TINYBMS_CAN_BITRATE` | `500000` | CAN bitrate in bps |
| `TINYBMS_STATUS_LED_PIN` | `2` | Optional LED toggled on CAN activity |
| `TINYBMS_SAMPLE_QUEUE_LENGTH` | `24` | Depth of the UART→CAN queue |
| `TINYBMS_KEEPALIVE_PERIOD_MS` | `1000` | Keepalive frame cadence |
| `TINYBMS_DIAGNOSTIC_PERIOD_MS` | `5000` | Interval for health logging |

## 🧠 Runtime behaviour

1. **UART task** – waits for newline-terminated TinyBMS telemetry lines, parses key/value pairs and enqueues validated samples.
2. **CAN task** – dequeues samples, converts them to Victron-oriented scaling (voltage ×100, current ×10, etc.) and transmits a standard frame (`0x355`). A keepalive (`0x351`) is emitted at the configured cadence.
3. **Diagnostics task** – periodically prints health counters (last UART byte, CAN transmissions, drops, errors).
4. **Wi-Fi manager** – provisions an access point (`TinyBMS`/`tinybms` by default) and optionally joins a configured infrastructure network.
5. **HTTP server** – serves `/` and static SPA assets from SPIFFS, exposes JSON endpoints for status/configuration and honours optional CORS headers.

## 📦 Legacy implementation

The previous Arduino + ESPAsyncWebServer firmware is preserved under `legacy/` for historical reference only. It is not referenced by the build system and all functionality has native ESP-IDF equivalents in the active firmware.

## 📄 License

MIT – see [LICENSE](LICENSE).
