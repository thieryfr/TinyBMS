# TinyBMS ↔ Victron ESP-IDF Bridge

The TinyBMS bridge is now a pure ESP-IDF application that ingests measurements from a TinyBMS battery monitor over UART and publishes compact Victron-compatible CAN frames using the native TWAI driver. The codebase no longer depends on the Arduino framework and can be built with the standard `idf.py` tooling or with PlatformIO in ESP-IDF-only mode.

## ✨ Highlights

- **ESP-IDF only** – UART, CAN (TWAI) and diagnostics use the native drivers, FreeRTOS tasks and esp_timer utilities.
- **Deterministic tasks** – dedicated UART, CAN and diagnostics tasks with configurable priorities and queue depth.
- **Text payload parser** – parses simple TinyBMS text telemetry (`V=...;I=...;SOC=...`) into structured samples.
- **Victron friendly CAN frames** – packs voltage/current/SOC/temperature into little-endian CAN data frames plus a keepalive PGN.
- **Runtime configurability** – UART/CAN pins, baudrate and diagnostic cadence are exposed in Kconfig (`menuconfig`).

## 🗂️ Repository layout

```
TinyBMS/
├── CMakeLists.txt          # ESP-IDF project entry point
├── main/
│   ├── app_main.cpp        # Application bootstrap (app_main)
│   ├── bridge.cpp          # UART ↔ CAN bridge implementation
│   ├── config.cpp          # Loads configuration from Kconfig values
│   ├── diagnostics.cpp     # Health counters and periodic logging
│   └── Kconfig.projbuild   # Custom configuration options
├── include/
│   ├── bridge.hpp
│   ├── config.hpp
│   └── diagnostics.hpp
├── legacy/                 # Previous Arduino-based sources kept for reference
│   ├── arduino_src/
│   └── arduino_include/
└── docs/, tests/, data/    # Unmodified project documentation and assets
```

## ⚙️ Building

### Using ESP-IDF tools

```bash
idf.py set-target esp32
idf.py menuconfig       # (optional) adjust pins / baudrate / queue depth
idf.py build
idf.py flash monitor
```

### Using PlatformIO

`platformio.ini` is configured for `framework = espidf`. From the repository root:

```bash
pio run
pio run -t upload
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
4. **Status LED** – optional GPIO toggled when CAN frames are published.

## 🧪 Testing

Unit tests for the new ESP-IDF implementation can be added with the `unity` runner. For now the project relies on manual validation with a TinyBMS unit and Victron CAN monitor. Contributions adding CI and test coverage are welcome.

## 📦 Legacy implementation

The previous Arduino + ESPAsyncWebServer firmware is preserved under `legacy/` for reference. It is no longer part of the build graph and can be removed in future releases once all features are ported.

## 📄 License

MIT – see [LICENSE](LICENSE).
