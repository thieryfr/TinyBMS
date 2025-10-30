# Critères de Non-Rupture - Migration ESP-IDF

**Document:** Guide des garanties de compatibilité ascendante
**Date:** 2025-10-30
**Principe:** **Aucune rupture de fonctionnalité ou de compatibilité utilisateur**

---

## 🎯 Principe Directeur

> **"Chaque phase doit préserver 100% des fonctionnalités existantes et maintenir la compatibilité avec les systèmes externes (Victron, TinyBMS, utilisateurs)"**

---

## ✅ Critères de Non-Rupture par Phase

### PHASE 1 : Fondations ESP-IDF

#### Critère Global
**Le système Arduino existant reste strictement inchangé et fonctionnel**

#### Critères Détaillés

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 1.1 | Build Arduino identique | Binaire byte-identical | `diff old.bin new.bin` |
| 1.2 | Aucun fichier Arduino modifié | Git diff vide | `git diff src/` |
| 1.3 | PlatformIO fonctionne | Compilation réussie | `pio run` |
| 1.4 | Tests existants passent | 100% pass | `pio test` |
| 1.5 | Déploiement Arduino OK | Flash + boot normal | `pio run -t upload` |

**✅ Garantie Phase 1:** L'utilisateur ne voit AUCUN changement (build ESP-IDF parallèle invisible)

---

### PHASE 2 : Migration Périphériques via HAL

#### Critère Global
**Tous les protocoles externes (UART, CAN, API, WiFi) restent identiques**

#### Critères Détaillés

**2.1 Interface UART (TinyBMS)**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 2.1.1 | Protocole Modbus RTU identique | Trames hexadécimales identiques | Analyser UART sniffer |
| 2.1.2 | Baudrate 19200 préservé | Config config.json respectée | Logs "UART initialized at 19200" |
| 2.1.3 | Registres lus identiques | 6 blocs (32+21, 102+2, etc.) | Comparer logs UART |
| 2.1.4 | Retry logic compatible | 3 tentatives par défaut | Logs retry count |
| 2.1.5 | Latence UART acceptable | < 150ms (P95) | Timestamp logs |
| 2.1.6 | CRC errors détectés | Logs "CRC error" présents | Tests avec trames corrompues |
| 2.1.7 | Timeout handling identique | 100ms par défaut | Config timeout_ms respectée |

**2.2 Interface CAN (Victron)**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 2.2.1 | Bitrate 500 kbps préservé | Config respectée | Logs "CAN initialized at 500000" |
| 2.2.2 | 9 PGNs Victron identiques | IDs 0x351, 0x355, ..., 0x382 | CAN sniffer |
| 2.2.3 | Encodage little-endian | Bytes identiques | Comparer trames CAN |
| 2.2.4 | Scaling Victron respecté | Valeurs identiques (V*10, A*10, etc.) | Victron GX affichage |
| 2.2.5 | Fréquence 1Hz préservée | Config can_update_interval_ms | Logs timestamps |
| 2.2.6 | Keep-alive 0x305 fonctionnel | Réponse Victron GX | Logs "keepalive OK" |
| 2.2.7 | Alarmes bitfield identiques | 8 bits 0x35A | Victron GX alarmes |

**2.3 Interface WiFi**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 2.3.1 | SSID/password depuis config.json | Connexion réussie | Logs "WiFi connected" |
| 2.3.2 | Hostname mDNS identique | `tinybms-bridge.local` | `ping tinybms-bridge.local` |
| 2.3.3 | Mode STA préservé | Config wifi.mode="sta" | Logs mode |
| 2.3.4 | IP DHCP ou static | Config ip_mode respectée | `curl http://IP/api/status` |
| 2.3.5 | Reconnexion auto | Max 10 tentatives | Logs retry |
| 2.3.6 | Fallback AP | Config ap_fallback.enabled | Test déconnexion |

**2.4 Interface Storage (SPIFFS)**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 2.4.1 | Fichiers existants lisibles | config.json, logs.txt | Logs "Config loaded" |
| 2.4.2 | Chemins identiques | `/spiffs/config.json`, etc. | Code paths inchangés |
| 2.4.3 | Format JSON compatible | ArduinoJson parse OK | Tests config reload |
| 2.4.4 | Écriture SPIFFS OK | Logs persistés | `GET /api/logs` |
| 2.4.5 | Taille partition 1MB | partitions.csv respectée | `idf.py partition-table` |

**2.5 API REST**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 2.5.1 | 17 endpoints identiques | URLs inchangées | Script curl 17 endpoints |
| 2.5.2 | JSON responses identiques | Schéma JSON identique | `jq` diff old/new |
| 2.5.3 | Status codes identiques | 200, 404, 500 appropriés | Tests HTTP |
| 2.5.4 | CORS headers présents | `Access-Control-Allow-Origin: *` | Browser DevTools |
| 2.5.5 | Hot-reload config OK | `POST /api/settings` appliqué | Tests reload |

**✅ Garantie Phase 2:** Victron GX, TinyBMS, clients API ne détectent AUCUN changement

---

### PHASE 3 : Migration WebServer

#### Critère Global
**Tous les endpoints API et WebSocket restent strictement identiques (comportement, latence acceptable)**

#### Critères Détaillés

**3.1 Endpoints API REST**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 3.1.1 | `GET /api/status` identique | JSON schéma identique | `jq` diff |
| 3.1.2 | `GET /api/settings` identique | Config complète retournée | Tests |
| 3.1.3 | `POST /api/settings` identique | Hot-reload fonctionne | Tests |
| 3.1.4 | `GET /api/logs` identique | Pagination identique | Tests |
| 3.1.5 | `DELETE /api/logs` identique | Purge fonctionne | Tests |
| 3.1.6 | `GET /api/diagnostics` identique | Heap, stack, EventBus | Tests |
| 3.1.7 | `GET /api/tinybms/registers` identique | Catalogue complet | Tests |
| 3.1.8 | `POST /api/tinybms/write` identique | Écriture registre OK | Tests |
| 3.1.9 | `GET /` (index.html) identique | Page web affichée | Browser |
| 3.1.10 | Latence API < 100ms | Response time acceptable | `curl -w "%{time_total}"` |

**3.2 WebSocket**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 3.2.1 | Endpoint `ws://host/ws` identique | Connexion réussie | Tests WebSocket |
| 3.2.2 | Protocole WebSocket RFC 6455 | Handshake standard | Browser DevTools |
| 3.2.3 | Broadcast 1Hz préservé | Config websocket_update_interval_ms | Tests timing |
| 3.2.4 | JSON payload identique | Schéma JSON identique | `jq` diff |
| 3.2.5 | Multi-clients (4 max) | 4 clients simultanés OK | Tests stress |
| 3.2.6 | Latence < 200ms (P95) | Acceptable pour UI | Tests stress |
| 3.2.7 | Reconnexion auto client | Après déconnexion | Tests |
| 3.2.8 | Ping/pong keep-alive | Pas de timeout | Tests longue durée |

**3.3 Interface Web UI**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 3.3.1 | Dashboard affiche données | Voltage, current, SOC, cells | Browser |
| 3.3.2 | Statistics graphiques OK | UART/CAN/EventBus charts | Browser |
| 3.3.3 | Settings éditeur fonctionne | Modification config | Browser |
| 3.3.4 | TinyBMS Config fonctionne | Read/write registres | Browser |
| 3.3.5 | Logs affichés | Pagination, filtres | Browser |

**✅ Garantie Phase 3:** Utilisateur web ne voit AUCUNE différence (sauf si latence légèrement différente mais < 200ms)

---

### PHASE 4 : Optimisation & Finalisation

#### Critère Global
**Toutes les métriques de performance restent dans les seuils acceptables**

#### Critères Détaillés

**4.1 Performance**

| # | Critère | Seuil Minimum | Validation |
|---|---------|---------------|------------|
| 4.1.1 | Latence UART→CAN | < 150ms (P95) | Logs timestamps |
| 4.1.2 | Latence WebSocket | < 200ms (P95) | Tests stress |
| 4.1.3 | Latence CAN TX | < 10ms | Logs timestamps |
| 4.1.4 | Charge CPU | < 60% | `GET /api/diagnostics` |
| 4.1.5 | Heap libre | > 150 KB | Monitor continu |
| 4.1.6 | EventBus queue | < 32 slots | Stats EventBus |

**4.2 Stabilité**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 4.2.1 | Uptime 24h sans crash | Test longrun réussit | Script 24h |
| 4.2.2 | Heap stable 24h | Aucun leak (>150KB) | Monitor heap |
| 4.2.3 | Aucun watchdog reset | Logs clean | Logs watchdog |
| 4.2.4 | Aucune tâche bloquée | Stack HWM OK | `uxTaskGetStackHighWaterMark` |
| 4.2.5 | SPIFFS stable | Logs écrits 24h | `GET /api/logs` |

**4.3 Compatibilité Binaire**

| # | Critère | Validation | Méthode |
|---|---------|------------|---------|
| 4.3.1 | Taille binaire < 1MB | Flash partition suffisante | `idf.py size` |
| 4.3.2 | Partition SPIFFS 1MB | Config préservée | `partition-table` |
| 4.3.3 | OTA possible | Espace OTA disponible | `partition-table` |

**✅ Garantie Phase 4:** Système stable 24h+, métriques ≥ baseline, prêt production

---

## 📊 Matrice de Validation Globale

### Tableau Récapitulatif

| Aspect | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|--------|---------|---------|---------|---------|
| **Build Arduino** | ✅ Inchangé | ⚠️ HAL switch | ❌ Supprimé | ❌ Supprimé |
| **Protocole UART** | ✅ Identique | ✅ Identique | ✅ Identique | ✅ Identique |
| **Protocole CAN** | ✅ Identique | ✅ Identique | ✅ Identique | ✅ Identique |
| **API REST** | ✅ Identique | ✅ Identique | ✅ Identique | ✅ Identique |
| **WebSocket** | ✅ Identique | ✅ Identique | ✅ Identique | ✅ Identique |
| **Config JSON** | ✅ Identique | ✅ Identique | ✅ Identique | ✅ Identique |
| **Latences** | ✅ Baseline | ⚠️ < 150ms | ⚠️ < 200ms | ✅ < seuils |
| **Stabilité** | ✅ Baseline | ⚠️ Tests 1h | ⚠️ Tests 5min | ✅ Tests 24h |

**Légende:**
- ✅ Aucun changement / dans seuils
- ⚠️ Changement interne / validation requise
- ❌ Supprimé (remplacé par équivalent)

---

## 🚨 Points de Vigilance Critiques

### Point de Vigilance 1: Latence WebSocket (Phase 3)

**Risque:** esp_http_server peut avoir latence > AsyncWebServer

**Critère Non-Rupture:**
- Latence P95 < 200ms (vs actuel 80-120ms)
- Tolérance: +66% maximum

**Validation:**
```bash
# Test stress 4 clients, 5 minutes
python tests/stress/websocket_stress.py
# Vérifier P95 latency dans output
```

**Plan B si échec:**
- Si P95 > 300ms: Rollback ESPAsyncWebServer
- Investiguer: profiling, buffer sizes, task priority
- Itérer architecture WebSocket

---

### Point de Vigilance 2: Latence UART (Phase 2)

**Risque:** twai_driver peut introduire latence CAN, impactant UART→CAN

**Critère Non-Rupture:**
- Latence UART→CAN P95 < 150ms (vs actuel 70-80ms)
- Tolérance: +87% maximum

**Validation:**
```bash
# Monitor logs avec timestamps
idf.py monitor | grep "UART poll\|CAN TX"
# Calculer delta timestamps
```

**Plan B si échec:**
- Si P95 > 200ms: Revoir buffer sizes TWAI
- Vérifier priority tâches CAN
- Profiler avec esp_timer_get_time()

---

### Point de Vigilance 3: Heap Stable (Phases 2-4)

**Risque:** Heap leak progressif (esp_http_server, SPIFFS, etc.)

**Critère Non-Rupture:**
- Heap libre > 150 KB constant (baseline 180-220 KB)
- Aucun leak détectable sur 24h

**Validation:**
```bash
# Monitor heap toutes les minutes
while true; do
  curl -s http://tinybms-bridge.local/api/diagnostics | jq '.heap_free'
  sleep 60
done
```

**Plan B si échec:**
- Si heap < 100 KB: Investiguer leak (heap tracing ESP-IDF)
- Limiter clients WebSocket (4 → 2)
- Réduire buffers JSON

---

## ✅ Checklist de Validation Finale

### Avant Release v3.0.0

**Validation Fonctionnelle**
- [ ] ✅ TinyBMS: 6 blocs registres lus correctement
- [ ] ✅ Victron: 9 PGNs CAN émis à 1Hz
- [ ] ✅ Victron GX: Affichage batterie OK (voltage, current, SOC)
- [ ] ✅ API REST: 17 endpoints répondent (200 OK)
- [ ] ✅ WebSocket: 4 clients connectés, broadcast 1Hz
- [ ] ✅ Web UI: Dashboard, Statistics, Settings, TinyBMS Config OK
- [ ] ✅ CVL Algorithm: Transitions états OK (BULK/FLOAT/etc.)
- [ ] ✅ Hot-reload config: POST /api/settings appliqué
- [ ] ✅ WiFi: Connexion STA + mDNS hostname

**Validation Performance**
- [ ] ✅ Latence UART→CAN: < 150ms (P95)
- [ ] ✅ Latence WebSocket: < 200ms (P95)
- [ ] ✅ Latence CAN TX: < 10ms
- [ ] ✅ Heap libre: > 150 KB constant
- [ ] ✅ Charge CPU: < 60%
- [ ] ✅ EventBus queue: < 32 slots, 0 overruns

**Validation Stabilité**
- [ ] ✅ Test 24h: aucun crash
- [ ] ✅ Test 24h: heap stable (> 150 KB)
- [ ] ✅ Test 24h: aucun watchdog reset
- [ ] ✅ Stress WebSocket 5min: aucune erreur
- [ ] ✅ Stress UART 1h: > 95% success rate
- [ ] ✅ Stress CAN 1h: 100% frames envoyés

**Validation Compatibilité**
- [ ] ✅ Config JSON v2.5.0 lisible par v3.0.0
- [ ] ✅ Victron GX ne détecte aucun changement
- [ ] ✅ TinyBMS communication identique
- [ ] ✅ Clients API existants fonctionnent
- [ ] ✅ Web UI identique (UX inchangée)

---

## 📚 Documentation Complémentaire

| Document | Usage |
|----------|-------|
| [PLAN_MIGRATION_ESP-IDF_PHASES.md](PLAN_MIGRATION_ESP-IDF_PHASES.md) | Plan détaillé technique |
| [SYNTHESE_MIGRATION_ESP-IDF.md](SYNTHESE_MIGRATION_ESP-IDF.md) | Vue exécutive |
| [ESP-IDF_MIGRATION_ANALYSIS.md](ESP-IDF_MIGRATION_ANALYSIS.md) | Analyse technique approfondie |

---

## 🎯 Résumé des Garanties

### Pour l'Utilisateur Final
> **"Aucun changement visible, configuration identique, protocoles identiques, performances équivalentes ou meilleures"**

### Pour Victron GX
> **"9 PGNs CAN identiques, encodage identique, fréquence identique, comportement identique"**

### Pour TinyBMS
> **"Protocole Modbus RTU identique, registres identiques, timing acceptable"**

### Pour les Développeurs
> **"APIs stables (HAL, EventBus, Config), build system change (PlatformIO → ESP-IDF), code métier inchangé (80%)"**

---

**Principe Final:** **Si un critère de non-rupture échoue, la phase est bloquée jusqu'à correction ou rollback.**

---

**Date:** 2025-10-30
**Auteur:** Claude (Migration Planning)
**Version:** 1.0
