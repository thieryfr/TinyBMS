# TinyBMS ESP-IDF Migration - Executive Summary

## Current State Assessment

**Project:** TinyBMS-Victron Bridge v2.5.0  
**Build System:** PlatformIO + Arduino Framework (NOT native ESP-IDF)  
**Code Quality:** Excellent (9.5/10 architecture score)  
**Total Code:** ~10,500 lines of C++  
**Status:** Production-ready, fully documented, tested

---

## Key Findings

### 1. Build System Overview

| Aspect | Current | Assessment |
|--------|---------|-----------|
| **Framework** | Arduino (wrapper) | Simple, but added abstraction layer |
| **Build Tool** | PlatformIO | Easy to use, good for embedded dev |
| **Board Support** | esp32dev | Generic ESP32 (WROOM/WROVER compatible) |
| **Optimization** | -O2 | Standard, could be -O3 for ESP-IDF |
| **C++ Standard** | C++17 | Modern, good library support |

**Current Config:**
```ini
[env:esp32can]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = ArduinoJson, ESPAsyncWebServer, AsyncTCP, CAN
```

### 2. Critical Library Dependencies

**MUST REPLACE for pure ESP-IDF:**
- ❌ **ESPAsyncWebServer** (Arduino-specific)
  - Current: Async HTTP/WebSocket server
  - Replacement: `esp_http_server` + custom WebSocket handler
  - Effort: **HIGH** (WebSocket is complex)
  - Risk: **MEDIUM** (potential latency impact)

- ❌ **AsyncTCP** (Arduino-specific)
  - Current: Non-blocking TCP stack
  - Replacement: Included in `esp_http_server`
  - Effort: Automatic if above is done

- ⚠️ **CAN Driver** (Arduino wrapper around twai)
  - Current: `sandeepmistry/CAN@^0.3.1`
  - Replacement: Direct `twai_driver` API
  - Effort: **MEDIUM** (straightforward driver)
  - Risk: **LOW**

**CAN KEEP for ESP-IDF:**
- ✅ **ArduinoJson** (cross-platform)
  - Current: v6.21.0, works on ESP-IDF natively
  - Effort: None

### 3. Hardware Dependencies Analysis

**Used ESP32 Peripherals:**
1. **UART** (Serial) - GPIO16/17
   - Current: Arduino Serial wrapper
   - ESP-IDF: `uart_driver.h` or keep Arduino variant
   - Complexity: LOW

2. **CAN Bus** - GPIO4/5
   - Current: Arduino CAN library wrapper
   - ESP-IDF: `driver/twai.h`
   - Complexity: LOW-MEDIUM

3. **SPIFFS Filesystem**
   - Current: Arduino SPIFFS wrapper
   - ESP-IDF: `esp_spiffs.h` + VFS
   - Complexity: LOW

4. **WiFi Stack**
   - Current: Arduino WiFi class
   - ESP-IDF: `esp_wifi.h`, `esp_netif.h`
   - Complexity: MEDIUM

5. **FreeRTOS**
   - Current: Via Arduino wrapper
   - ESP-IDF: Direct access (same kernel)
   - Complexity: LOW (already being used)

**Performance Impact:** Minimal for UART/CAN/SPIFFS, moderate for WiFi

### 4. Architecture Assessment

**Strengths for Migration:**
✅ **HAL abstraction layer** already in place
  - Interfaces: IHalUart, IHalCan, IHalStorage, IHalGpio, IHalTimer, IHalWatchdog
  - Factory pattern for implementations
  - Mock implementations for testing
  - Can swap ESP32Factory for ESP-IDF implementations

✅ **Event-driven architecture** is framework-agnostic
  - EventBus V2 uses only C++ standard library
  - No Arduino-specific dependencies
  - Will work unchanged with ESP-IDF

✅ **Configuration system** is framework-agnostic
  - JSON-based (ArduinoJson)
  - SPIFFS abstraction via HAL
  - Will work with minimal changes

✅ **FreeRTOS task structure** ready for ESP-IDF
  - Already using direct FreeRTOS API (xTaskCreate, xSemaphore, etc.)
  - No Arduino task abstraction

**Challenges for Migration:**
⚠️ **Web server stack** tightly coupled to Arduino
  - ESPAsyncWebServer is core to REST API
  - WebSocket broadcast is critical feature
  - No abstraction layer (unlike HAL for peripherals)

⚠️ **WiFi initialization** in Arduino style
  - `system_init.cpp` uses Arduino WiFi class
  - Would need rewrite for esp_wifi API

⚠️ **Serial logging** uses Arduino Serial
  - Currently works but should use esp_log for production

### 5. Module Breakdown (46 source files)

**Core Modules** (Framework-independent):
- ✅ EventBus V2 (100% portable)
- ✅ ConfigManager (100% portable, uses HAL for storage)
- ✅ CVL Logic (100% portable, pure algorithm)
- ✅ Watchdog Manager (mostly portable, wraps FreeRTOS)
- ✅ Logger (mostly portable, uses HAL)

**HAL-Dependent Modules** (Manageable):
- ⚠️ UART Bridge (via HAL - portable with new HAL impl)
- ⚠️ CAN Bridge (via HAL - needs twai driver)
- ⚠️ System Init (WiFi init - needs rewrite)

**Arduino-Dependent Modules** (Risky):
- ❌ Web Routes & WebSocket (AsyncWebServer)
- ❌ Web Server Setup (AsyncWebServer)

**Total Impact:**
- 80% of code is framework-independent
- 20% of code requires significant rewrite (web stack)

### 6. Recommended Migration Strategy

#### **Phase 1: Prepare (1-2 weeks)**
1. Create ESP-IDF compatible HAL implementations
   - esp32_uart_idf.cpp (using uart_driver API)
   - esp32_can_idf.cpp (using twai_driver API)
   - esp32_storage_idf.cpp (using SPIFFS VFS)
   - esp32_wifi_idf.cpp (using esp_wifi API)

2. Migrate build configuration
   - Create CMakeLists.txt for ESP-IDF
   - Keep platformio.ini as fallback
   - Create idf_component.yml

3. Create wrapper for esp_log
   - Implement Logger using esp_log
   - Maintain compatibility with existing code

**Effort:** 1-2 weeks (parallel to current development)
**Risk:** LOW (non-breaking, can run both systems)

#### **Phase 2: Replace Web Stack (2-3 weeks)**
1. Implement web server with `esp_http_server`
   - Create WebSocketHandler wrapping http_server
   - Implement JSON response building
   - Port REST endpoints one by one

2. Replace AsyncWebServer usage
   - Refactor web_routes_api.cpp
   - Refactor websocket_handlers.cpp
   - Refactor web_server_setup.cpp

3. Testing & validation
   - Load test with multiple WebSocket clients
   - Verify latency (current: 80-120ms)

**Effort:** 2-3 weeks (blocking, critical path)
**Risk:** MEDIUM (potential latency issues)

#### **Phase 3: Polish (1 week)**
1. Optimize for ESP-IDF
   - Use esp_log instead of Serial logger
   - Enable flash encryption
   - Optimize heap/stack usage

2. Remove Arduino dependencies
   - Replace Serial with esp_log
   - Use FreeRTOS API directly
   - Clean up build system

3. Comprehensive testing
   - End-to-end tests on hardware
   - Performance benchmarking
   - Long-run stability tests

**Effort:** 1 week
**Risk:** LOW

### 7. Testing Strategy

**Unit Tests (No Changes):**
- Native C++ tests for CVL algorithm
- Mock-based tests for HAL interfaces
- ArduinoJson tests (unchanged)

**Integration Tests (Need Updates):**
- Python end-to-end tests
- Update ESP32 mock factory → ESP-IDF mock factory
- WebSocket stress tests

**Hardware Tests (New):**
- Real ESP32 with TinyBMS
- Real CAN bus with Victron GX
- WiFi/MQTT connectivity
- Long-run (24h) stability test

### 8. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|-----------|
| **WebSocket latency increases** | Medium | High | Phased approach, benchmarking |
| **WiFi connectivity issues** | Low | Medium | Use proven esp_wifi API |
| **CAN driver incompatibility** | Low | Low | twai_driver is well-tested |
| **UART/Serial problems** | Low | Low | HAL abstraction encapsulates |
| **Build system complexity** | Medium | Low | CMakeLists.txt well documented |
| **Memory/heap growth** | Medium | Medium | Profile at each phase |

### 9. Effort Estimate

| Phase | Duration | Full-Time Dev Days |
|-------|----------|-------------------|
| Phase 1: Prepare | 1-2 weeks | 5-10 days |
| Phase 2: Web Stack | 2-3 weeks | 8-15 days |
| Phase 3: Polish | 1 week | 3-5 days |
| **Total** | **4-6 weeks** | **16-30 days** |

**Can be parallelized:** Phase 1 can run alongside current development

### 10. Go/No-Go Decision Points

**Phase 1 → Phase 2:**
- ✅ All HAL implementations passing unit tests
- ✅ No regression in existing features
- ✅ Build system stable (both PlatformIO and ESP-IDF)

**Phase 2 → Phase 3:**
- ✅ esp_http_server running on hardware
- ✅ WebSocket clients connecting successfully
- ✅ API endpoints responding with correct data
- ✅ Latency within acceptable range (target: <150ms)

**Final Release:**
- ✅ 24h stability test passed
- ✅ All previous tests still passing
- ✅ Documentation updated
- ✅ Performance metrics documented

---

## Key Metrics to Preserve

**Must NOT Degrade:**
- UART polling latency: Current 70-80ms → Target <100ms
- CAN transmission: Current 2-5ms → Target <10ms
- WebSocket latency: Current 80-120ms → Target <200ms
- Heap free: Current 180-220KB → Target >150KB minimum
- Binary size: Current ~500KB → Target <800KB (ESP-IDF is larger)

---

## Recommendation

**✅ GO for ESP-IDF Migration**

The codebase is **well-architected** for this migration:
1. HAL abstraction already in place
2. Event-driven core is framework-independent
3. 80% of code requires zero changes
4. Phased approach minimizes risk
5. Clear success criteria at each phase

**Best approach:** Gradual migration (Phase 1) while maintaining PlatformIO support, followed by selective component replacement (Phase 2).

**Timeline:** 4-6 weeks with 1 developer (part-time possible for Phase 1)

---

## See Also
- `/docs/ESP-IDF_MIGRATION_ANALYSIS.md` - Detailed technical analysis
- `/docs/architecture.md` - Current architecture patterns
- `/README.md` - Project overview

