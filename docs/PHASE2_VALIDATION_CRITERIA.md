# Phase 2 - Critères de Validation Détaillés

**Phase:** 2 - Migration Périphériques via HAL
**Date:** 2025-10-30
**Référence:** [CRITERES_NON_RUPTURE_MIGRATION.md](CRITERES_NON_RUPTURE_MIGRATION.md)

---

## 🎯 Vue d'Ensemble

Cette phase bascule les périphériques (UART, CAN, Storage) vers HAL ESP-IDF natif tout en maintenant:
- ✅ WebServer Arduino (ESPAsyncWebServer)
- ✅ Configuration JSON identique
- ✅ API REST identique
- ✅ Protocoles CAN/UART identiques

---

## ✅ Critères de Validation (10 points)

### 2.1 UART Polling Fonctionne

**Objectif:** TinyBMS communication via UART native ESP-IDF

**Méthode:**
```bash
# Monitoring 5 minutes
pio device monitor | tee uart_test.log

# Analyse
grep "UART poll success" uart_test.log | wc -l
# Attendu: ~3000 (10Hz * 300s)

grep "UART error" uart_test.log | wc -l
# Attendu: < 150 (< 5% errors)
```

**Seuil:** > 95% success rate

**Détails Logs:**
```
I (12345) ESP32UartIDF: UART2 initialized: RX=16, TX=17, baud=19200
[UART] Poll #1 success (78ms)
[UART] Poll #2 success (81ms)
...
[UART] Success rate: 98.5% (2950/3000)
```

---

### 2.2 CAN TX Réussit

**Objectif:** Victron CAN frames émis via TWAI native

**Méthode:**
```bash
# Monitoring 2 minutes
pio device monitor | grep "CAN TX"

# Vérifier
# - 9 PGNs émis (0x351, 0x355, ..., 0x382)
# - Fréquence 1Hz
# - Aucune erreur TX
```

**Seuil:** 100% frames envoyés (0 errors)

**Détails Logs:**
```
I (23456) ESP32CanIDF: CAN initialized: TX=4, RX=5, bitrate=500000
[CAN] TX frame 0x351 (8 bytes) - OK
[CAN] TX frame 0x355 (8 bytes) - OK
...
[CAN] TX success: 1080/1080 (100%)
```

---

### 2.3 Latence UART→CAN

**Objectif:** Latence end-to-end < 150ms (P95)

**Méthode:**
```bash
# Script analyse latence
cat > analyze_latency.py <<'EOF'
import re
import sys

latencies = []
with open('uart_can.log') as f:
    uart_time = None
    for line in f:
        if 'UART poll success' in line:
            match = re.search(r'\((\d+)ms\)', line)
            if match:
                uart_time = int(match.group(1))
        elif 'CAN TX' in line and uart_time:
            match = re.search(r'\((\d+)ms\)', line)
            if match:
                can_time = int(match.group(1))
                latency = can_time - uart_time
                if 0 < latency < 500:  # Sanity check
                    latencies.append(latency)
                uart_time = None

latencies.sort()
p95 = latencies[int(len(latencies) * 0.95)]
print(f"P50: {latencies[len(latencies)//2]}ms")
print(f"P95: {p95}ms")
print(f"Max: {max(latencies)}ms")
EOF

python analyze_latency.py
```

**Seuil:** P95 < 150ms

**Baseline:**
- Arduino HAL: P95 = 80ms
- Tolérance: +87% = 150ms

---

### 2.4 Config JSON Reload

**Objectif:** Hot-reload configuration via API

**Méthode:**
```bash
# Test 1: Modifier CVL enabled
curl -X POST http://tinybms-bridge.local/api/settings \
  -H "Content-Type: application/json" \
  -d '{"cvl":{"enabled":false}}'

# Vérifier
curl http://tinybms-bridge.local/api/settings | jq '.cvl.enabled'
# Attendu: false

# Test 2: Modifier UART baudrate (nécessite reboot)
curl -X POST http://tinybms-bridge.local/api/settings \
  -H "Content-Type: application/json" \
  -d '{"hardware":{"uart":{"baudrate":9600}}}'

# Vérifier logs après reboot
# I (xxx) ESP32UartIDF: UART2 initialized: ..., baud=9600
```

**Seuil:** Config appliquée immédiatement (ou après reboot si hardware)

---

### 2.5 WiFi Reconnect

**Objectif:** Reconnexion WiFi automatique

**Méthode:**
```bash
# Test déconnexion WiFi
# 1. Désactiver AP WiFi ou changer password
# 2. Observer logs
# 3. Réactiver AP
# 4. Mesurer temps reconnexion

# Logs attendus:
# [WiFi] Disconnected
# [WiFi] Reconnecting... (attempt 1/10)
# [WiFi] Reconnecting... (attempt 2/10)
# [WiFi] Connected! IP: 192.168.1.100
# [WiFi] Reconnection time: 8.5s
```

**Seuil:** < 10s reconnexion

---

### 2.6 SPIFFS Read/Write

**Objectif:** Persistence SPIFFS via esp_spiffs natif

**Méthode:**
```bash
# Test 1: Écriture logs
curl http://tinybms-bridge.local/api/logs | head -20
# Doit afficher logs récents

# Test 2: Écriture config
curl -X POST http://tinybms-bridge.local/api/settings \
  -d '{"test_key":"test_value"}'

# Reboot ESP32
# pio run -t upload

# Vérifier persistence
curl http://tinybms-bridge.local/api/settings | jq '.test_key'
# Attendu: "test_value"

# Test 3: Capacité SPIFFS
curl http://tinybms-bridge.local/api/diagnostics | jq '.spiffs'
# Attendu: {"total_kb":1024,"used_kb":128}
```

**Seuil:** Read/Write/Persist OK

---

### 2.7 EventBus Stats

**Objectif:** Aucune perte d'événements

**Méthode:**
```bash
# Monitor EventBus stats
curl http://tinybms-bridge.local/api/diagnostics | jq '.eventbus'

# Attendu:
# {
#   "total_published": 36000,
#   "queue_size": 32,
#   "queue_used": 3,
#   "queue_overruns": 0  # ← CRITIQUE
# }
```

**Seuil:** `queue_overruns = 0`

**Si overruns > 0:**
- Augmenter `EVENTBUS_QUEUE_SIZE`
- Réduire fréquence publication
- Optimiser subscribers

---

### 2.8 Heap Stable

**Objectif:** Pas de memory leak sur 1h

**Méthode:**
```bash
# Script monitoring heap
cat > monitor_heap.sh <<'EOF'
#!/bin/bash
LOG_FILE="heap_$(date +%Y%m%d_%H%M%S).log"
DURATION_H=1

echo "Starting heap monitoring for ${DURATION_H}h..."
echo "timestamp,heap_free,heap_min" > $LOG_FILE

END=$((SECONDS + DURATION_H * 3600))
while [ $SECONDS -lt $END ]; do
    TIMESTAMP=$(date +%s)
    DATA=$(curl -s http://tinybms-bridge.local/api/diagnostics)
    HEAP=$(echo $DATA | jq '.heap_free')
    HEAP_MIN=$(echo $DATA | jq '.heap_min_free')

    echo "$TIMESTAMP,$HEAP,$HEAP_MIN" >> $LOG_FILE
    echo "$(date): Heap=$HEAP KB, Min=$HEAP_MIN KB"

    sleep 60
done

echo "Heap monitoring completed. Log: $LOG_FILE"

# Analyse
python3 <<PYTHON
import pandas as pd
df = pd.read_csv('$LOG_FILE')
print(f"Heap stats:")
print(f"  Min: {df.heap_free.min()} KB")
print(f"  Max: {df.heap_free.max()} KB")
print(f"  Mean: {df.heap_free.mean():.1f} KB")
print(f"  Std: {df.heap_free.std():.1f} KB")

# Check leak (heap décroissant)
if df.heap_free.iloc[-1] < df.heap_free.iloc[0] - 10000:
    print("⚠️  WARNING: Possible memory leak detected")
else:
    print("✅ Heap stable")
PYTHON
EOF

chmod +x monitor_heap.sh
./monitor_heap.sh
```

**Seuil:** Heap > 150 KB constant (± 10KB)

---

### 2.9 Watchdog OK

**Objectif:** Aucun reset watchdog pendant 1h

**Méthode:**
```bash
# Monitor watchdog
pio device monitor | grep -E "WDT|reset|reboot" | tee watchdog.log

# Run 1h
# Attendu: Aucune ligne "WDT timeout" ou "reset"

# Vérifier stats watchdog
curl http://tinybms-bridge.local/api/diagnostics | jq '.watchdog'

# Attendu:
# {
#   "feed_count": 3600,
#   "min_interval_ms": 950,
#   "max_interval_ms": 1050,
#   "average_interval_ms": 1000
# }
```

**Seuil:** 0 reset watchdog sur 1h

---

### 2.10 API REST Répond

**Objectif:** Tous les endpoints API REST fonctionnels

**Méthode:**
```bash
# Script test tous endpoints
cat > test_api.sh <<'EOF'
#!/bin/bash
BASE_URL="http://tinybms-bridge.local"

ENDPOINTS=(
    "/api/status"
    "/api/settings"
    "/api/logs"
    "/api/diagnostics"
    "/api/tinybms/registers"
    "/api/cvl/state"
    "/api/eventbus/stats"
    "/api/system/info"
    "/"
)

PASS=0
FAIL=0

for endpoint in "${ENDPOINTS[@]}"; do
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" $BASE_URL$endpoint)

    if [ "$HTTP_CODE" = "200" ]; then
        echo "✅ $endpoint - OK"
        ((PASS++))
    else
        echo "❌ $endpoint - FAIL (HTTP $HTTP_CODE)"
        ((FAIL++))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && echo "✅ All API tests passed" || echo "❌ Some API tests failed"
EOF

chmod +x test_api.sh
./test_api.sh
```

**Seuil:** 100% endpoints OK (HTTP 200)

---

## 📊 Résumé Validation

**Tableau de Bord:**

| # | Critère | Statut | Valeur Mesurée | Seuil | OK? |
|---|---------|--------|----------------|-------|-----|
| 2.1 | UART polling | ⬜ | ___ % | > 95% | ⬜ |
| 2.2 | CAN TX | ⬜ | ___ % | 100% | ⬜ |
| 2.3 | Latence UART→CAN | ⬜ | ___ ms | < 150ms | ⬜ |
| 2.4 | Config reload | ⬜ | OK/FAIL | OK | ⬜ |
| 2.5 | WiFi reconnect | ⬜ | ___ s | < 10s | ⬜ |
| 2.6 | SPIFFS R/W | ⬜ | OK/FAIL | OK | ⬜ |
| 2.7 | EventBus overruns | ⬜ | ___ | 0 | ⬜ |
| 2.8 | Heap stable | ⬜ | ___ KB | > 150KB | ⬜ |
| 2.9 | Watchdog resets | ⬜ | ___ | 0 | ⬜ |
| 2.10 | API endpoints | ⬜ | ___ / 9 | 9/9 | ⬜ |

**Go/No-Go Phase 2→3:** Tous critères ✅ (10/10)

---

## 🚨 Actions si Critère Échoue

### Si 2.1 (UART) ou 2.2 (CAN) échoue

**Rollback:** Désactiver `USE_ESP_IDF_HAL`
**Investigation:**
- Vérifier GPIO pins (RX/TX, CAN TX/RX)
- Vérifier baudrate/bitrate
- Tester loopback hardware
- Vérifier terminaisons CAN

**Timeline:** 1-2 jours investigation

### Si 2.3 (Latence) échoue

**Rollback:** Non requis si < 200ms
**Investigation:**
- Profiler code (esp_timer_get_time())
- Vérifier priorités tâches FreeRTOS
- Augmenter buffer sizes UART/CAN
- Activer CONFIG_UART_ISR_IN_IRAM

**Timeline:** 2-3 jours optimisation

### Si 2.8 (Heap) échoue

**Rollback:** Désactiver `USE_ESP_IDF_HAL`
**Investigation:**
- Activer heap tracing (CONFIG_HEAP_TRACING)
- Identifier leaks avec valgrind-like tools
- Réduire buffer sizes
- Limiter cache EventBus

**Timeline:** 3-5 jours debugging

---

## 📚 Documentation

- **README_ESP-IDF_PHASE2.md** - Guide utilisateur Phase 2
- **PLAN_MIGRATION_ESP-IDF_PHASES.md** - Plan complet
- **CRITERES_NON_RUPTURE_MIGRATION.md** - Tous critères migration

---

**Date:** 2025-10-30
**Auteur:** Claude (ESP-IDF Migration Phase 2)
