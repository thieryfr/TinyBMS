# TinyBMS Testing Suite

Phase 4: Comprehensive testing infrastructure for ESP-IDF migration validation.

## Overview

This directory contains test scripts for validating TinyBMS system stability, performance, and correctness.

## Test Categories

### Stability Tests (`stability/`)

Long-term stability and endurance testing.

#### 24-Hour Stability Test

**Script:** `stability/longrun_test.sh`

**Purpose:** Validates system stability over 24 hours of continuous operation.

**Monitors:**
- API endpoint availability and latency
- WebSocket connectivity
- Heap memory stability (>150KB threshold)
- System uptime and watchdog health

**Usage:**
```bash
cd tests/stability
./longrun_test.sh tinybms-bridge.local 24
```

**Success Criteria:**
- Zero critical heap warnings (< 100KB)
- < 1% API failure rate
- No unexpected reboots
- Heap remains > 150KB throughout test

**Output:**
- `longrun_YYYYMMDD_HHMMSS.log` - Detailed timestamped log
- `longrun_YYYYMMDD_HHMMSS.csv` - Time-series data for graphing

### Stress Tests (`stress/`)

High-load and concurrent connection testing.

#### WebSocket Stress Test

**Script:** `stress/websocket_stress.py`

**Purpose:** Validates WebSocket server performance under concurrent client load.

**Tests:**
- Multiple concurrent WebSocket connections (default 4 clients)
- Message receive rate and reliability
- Connection stability over time
- Error handling under load

**Requirements:**
```bash
pip install websockets
```

**Usage:**
```bash
cd tests/stress
python websocket_stress.py tinybms-bridge.local 4 300
```

**Parameters:**
1. Device IP/hostname (default: tinybms-bridge.local)
2. Number of concurrent clients (default: 4)
3. Test duration in seconds (default: 300)

**Success Criteria:**
- All clients receive messages continuously
- Error rate < 5%
- No unexpected disconnections
- Average receive rate > 0.8 msg/s per client

## Test Execution Guidelines

### Pre-Test Checklist

Before running any tests:

1. **Verify device is accessible:**
   ```bash
   ping tinybms-bridge.local
   curl http://tinybms-bridge.local/api/status
   ```

2. **Check initial system health:**
   ```bash
   curl http://tinybms-bridge.local/api/memory | jq
   ```

3. **Ensure TinyBMS is connected:**
   - Verify UART connection to TinyBMS
   - Verify CAN connection to Victron system

4. **Monitor during tests:**
   - Keep `idf.py monitor` or serial console open
   - Watch for unexpected reboots or watchdog triggers

### Post-Test Analysis

After test completion:

1. **Review logs:**
   ```bash
   grep ERROR longrun_*.log
   grep WARN longrun_*.log
   ```

2. **Analyze heap stability:**
   ```bash
   # Plot heap over time (requires gnuplot)
   gnuplot -e "set terminal png; set output 'heap.png'; \
               plot 'longrun_*.csv' using 1:2 with lines title 'Free Heap KB'"
   ```

3. **Check for memory leaks:**
   - Compare initial and final heap values
   - Look for downward trend in CSV data

4. **Validate watchdog health:**
   ```bash
   curl http://tinybms-bridge.local/api/watchdog | jq
   ```

## Troubleshooting

### Common Issues

**Issue:** `longrun_test.sh` reports API failures

**Solution:**
- Check network connectivity
- Verify device hasn't rebooted (check uptime)
- Review serial console for errors

**Issue:** `websocket_stress.py` clients can't connect

**Solution:**
- Verify WebSocket is enabled (`USE_ESP_IDF_WEBSERVER` or AsyncWebServer)
- Check max client limit in config (default: 4)
- Ensure WiFi is stable

**Issue:** Heap warnings during tests

**Solution:**
- Reduce concurrent WebSocket clients
- Check for memory leaks in application code
- Review task stack sizes

## Performance Baselines

### Expected Metrics (ESP-IDF Build)

| Metric | Target | Warning | Critical |
|--------|--------|---------|----------|
| Free Heap | > 200 KB | < 150 KB | < 100 KB |
| API Latency P95 | < 500ms | < 1000ms | > 1000ms |
| WebSocket Latency P95 | < 200ms | < 500ms | > 1000ms |
| Uptime MTBF | > 24h | < 24h | < 12h |

### Comparison: Arduino vs ESP-IDF

| Metric | v2.5.0 (Arduino) | v3.0.0 (ESP-IDF) | Delta |
|--------|------------------|------------------|-------|
| Free Heap | 180-220 KB | 200-250 KB | +10% |
| API Latency | 70-80ms | 80-100ms | +15% |
| Flash Size | ~500 KB | ~800 KB | +60% |
| Boot Time | 3-4s | 2-3s | -25% |

## CI/CD Integration

These tests can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions workflow
- name: Run stability test
  run: |
    tests/stability/longrun_test.sh $DEVICE_IP 1  # 1-hour test for CI
```

## Contributing

When adding new tests:

1. Place in appropriate category (`stability/`, `stress/`, `unit/`, etc.)
2. Include clear usage instructions in script header
3. Document success criteria
4. Update this README

## References

- [ESP-IDF Testing Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
- [TinyBMS Migration Plan](../docs/PLAN_MIGRATION_ESP-IDF_PHASES.md)
- [Phase 4 Validation Criteria](../docs/PLAN_MIGRATION_ESP-IDF_PHASES.md#phase-4)
