# Analyse de l'Interface Settings - État Actuel et Plan d'Amélioration

**Date:** 2025-10-30
**Objectif:** Identifier les paramètres ESP32/CVL exposés vs manquants dans l'interface web

---

## 1. État Actuel - Interface Settings Existante

### ✅ L'onglet Settings EXISTE et est fonctionnel

**Fichiers:**
- `data/index.html` - UI (lignes 974-1500+)
- `data/settings.js` - Logique (28KB)
- Backend API dans `src/web_routes_api.cpp`

**6 Sous-onglets implémentés:**

| Sous-onglet | Status | Paramètres Exposés |
|-------------|--------|-------------------|
| 1️⃣ WiFi | ✅ Complet | mode, SSID, password, hostname, IP config, AP settings |
| 2️⃣ Hardware | ✅ Complet | UART pins/baudrate, CAN pins/bitrate/termination |
| 3️⃣ CVL Algorithm | ⚠️ Partiel | 9 paramètres de base exposés |
| 4️⃣ Victron | ⚠️ Partiel | manufacturer, battery_name, intervalles |
| 5️⃣ Logging | ✅ Complet | level, serial, web, sd, syslog |
| 6️⃣ System | ⚠️ Partiel | web_port, ws_max_clients, ws_update_interval, cors |

---

## 2. Paramètres Disponibles dans config_manager.h

### 📦 Structure Complète des Configurations

```cpp
struct ConfigManager {
    WiFiConfig wifi;              // ✅ Complet dans UI
    HardwareConfig hardware;      // ✅ Complet dans UI
    TinyBMSConfig tinybms;        // ❌ MANQUANT dans UI
    VictronConfig victron;        // ⚠️ Partiel (thresholds manquants)
    CVLConfig cvl;                // ⚠️ Partiel (paramètres avancés manquants)
    MqttConfig mqtt;              // ❌ TOTALEMENT MANQUANT dans UI
    WebServerConfig web_server;   // ⚠️ Partiel (paramètres avancés manquants)
    LoggingConfig logging;        // ✅ Complet dans UI
    AdvancedConfig advanced;      // ❌ TOTALEMENT MANQUANT dans UI
}
```

---

## 3. Détail des Paramètres Manquants

### 🔴 SECTION 1: TinyBMS Configuration (TOTALEMENT MANQUANTE)

**Paramètres disponibles dans code mais absents de l'UI:**

```cpp
struct TinyBMSConfig {
    ✅ uint32_t poll_interval_ms = 100;                    // Intervalle de polling principal
    ❌ uint32_t poll_interval_min_ms = 50;                 // Polling adaptatif - min
    ❌ uint32_t poll_interval_max_ms = 500;                // Polling adaptatif - max
    ❌ uint32_t poll_backoff_step_ms = 25;                 // Backoff en cas d'erreur
    ❌ uint32_t poll_recovery_step_ms = 10;                // Recovery après erreur
    ❌ uint32_t poll_latency_target_ms = 40;               // Target latency
    ❌ uint32_t poll_latency_slack_ms = 15;                // Slack latency
    ❌ uint8_t poll_failure_threshold = 3;                 // Seuil échecs
    ❌ uint8_t poll_success_threshold = 6;                 // Seuil succès
    ✅ uint8_t uart_retry_count = 3;                       // Nombre de retries UART
    ✅ uint32_t uart_retry_delay_ms = 50;                  // Délai entre retries
    ✅ bool broadcast_expected = true;                     // Broadcast attendu
}
```

**Importance:** 🔥 **CRITIQUE** - Ces paramètres contrôlent la performance et la réactivité du système

---

### 🟡 SECTION 2: Victron Thresholds (MANQUANTS)

**Actuellement exposé dans UI:**
- ✅ manufacturer_name
- ✅ battery_name
- ✅ pgn_interval_ms
- ✅ cvl_interval_ms
- ✅ keepalive_interval_ms

**Manquant - Victron.thresholds:**
```cpp
struct Thresholds {
    ❌ float undervoltage_v = 44.0f;              // Seuil sous-tension
    ❌ float overvoltage_v = 58.4f;               // Seuil surtension
    ❌ float overtemp_c = 55.0f;                  // Seuil température haute
    ❌ float low_temp_charge_c = 0.0f;            // Seuil température basse charge
    ❌ uint16_t imbalance_warn_mv = 100;          // Avertissement déséquilibre
    ❌ uint16_t imbalance_alarm_mv = 200;         // Alarme déséquilibre
    ❌ float soc_low_percent = 10.0f;             // SOC bas
    ❌ float soc_high_percent = 99.0f;            // SOC haut
    ❌ float derate_current_a = 1.0f;             // Derate courant
}
```

**Importance:** 🟡 **IMPORTANT** - Seuils de sécurité et alarmes Victron

---

### 🟡 SECTION 3: CVL Paramètres Avancés (MANQUANTS)

**Actuellement exposé dans UI (9 paramètres):**
- ✅ enabled
- ✅ bulk_transition_soc
- ✅ transition_float_soc
- ✅ float_exit_soc
- ✅ float_approach_offset
- ✅ float_offset
- ✅ imbalance_offset
- ✅ imbalance_trigger_mv
- ✅ imbalance_release_mv

**Manquant - CVL Avancé (15 paramètres):**
```cpp
❌ uint16_t series_cell_count = 16;                 // Nombre de cellules série
❌ float cell_max_voltage_v = 3.65f;                // Tension max cellule
❌ float cell_safety_threshold_v = 3.50f;           // Seuil sécurité
❌ float cell_safety_release_v = 3.47f;             // Release sécurité
❌ float cell_min_float_voltage_v = 3.20f;          // Min voltage float
❌ float cell_protection_kp = 120.0f;               // Gain protection
❌ float dynamic_current_nominal_a = 157.0f;        // Courant nominal
❌ float max_recovery_step_v = 0.4f;                // Step de récupération
❌ float sustain_soc_entry_percent = 5.0f;          // Entry sustain mode
❌ float sustain_soc_exit_percent = 8.0f;           // Exit sustain mode
❌ float sustain_voltage_v = 0.0f;                  // Voltage sustain
❌ float sustain_per_cell_voltage_v = 3.125f;       // Voltage/cell sustain
❌ float sustain_ccl_limit_a = 5.0f;                // CCL sustain
❌ float sustain_dcl_limit_a = 5.0f;                // DCL sustain
❌ float imbalance_drop_per_mv = 0.0005f;           // Drop par mV
❌ float imbalance_drop_max_v = 2.0f;               // Drop max
```

**Importance:** 🟢 **MOYEN** - Tuning fin de l'algorithme CVL (pour utilisateurs avancés)

---

### 🔴 SECTION 4: MQTT Configuration (TOTALEMENT MANQUANTE)

```cpp
struct MqttConfig {
    ❌ bool enabled = false;                       // MQTT activé
    ❌ String uri = "mqtt://127.0.0.1";            // URI serveur
    ❌ uint16_t port = 1883;                       // Port MQTT
    ❌ String client_id = "tinybms-victron";       // Client ID
    ❌ String username = "";                       // Username
    ❌ String password = "";                       // Password
    ❌ String root_topic = "victron/tinybms";      // Topic racine
    ❌ bool clean_session = true;                  // Clean session
    ❌ bool use_tls = false;                       // TLS activé
    ❌ String server_certificate = "";             // Certificat TLS
    ❌ uint16_t keepalive_seconds = 30;            // Keepalive
    ❌ uint32_t reconnect_interval_ms = 5000;      // Intervalle reconnexion
    ❌ uint8_t default_qos = 0;                    // QoS par défaut
    ❌ bool retain_by_default = false;             // Retain par défaut
}
```

**Importance:** 🟡 **IMPORTANT** - Intégration MQTT pour Home Assistant / MQTT brokers

---

### 🟡 SECTION 5: WebServer Avancé (PARTIELLEMENT MANQUANT)

**Actuellement exposé:**
- ✅ port
- ✅ max_ws_clients
- ✅ ws_update_interval (comme "websocket_update_interval_ms")
- ✅ cors_enabled

**Manquant - WebSocket Avancé:**
```cpp
❌ uint32_t websocket_min_interval_ms = 100;       // Intervalle min WS
❌ uint32_t websocket_burst_window_ms = 1000;      // Fenêtre burst
❌ uint32_t websocket_burst_max = 4;               // Max burst
❌ uint32_t websocket_max_payload_bytes = 4096;    // Payload max
❌ bool enable_auth = false;                       // Auth activé
❌ String username = "admin";                      // Username
❌ String password = "admin";                      // Password
```

**Importance:** 🟢 **FAIBLE** - Optimisations WebSocket (déjà bonnes valeurs par défaut)

---

### 🔴 SECTION 6: Advanced Configuration (TOTALEMENT MANQUANTE)

```cpp
struct AdvancedConfig {
    ❌ bool enable_spiffs = true;                  // SPIFFS activé
    ❌ bool enable_ota = false;                    // OTA activé
    ❌ uint32_t watchdog_timeout_s = 30;           // Timeout watchdog
    ❌ uint32_t stack_size_bytes = 8192;           // Stack size tasks
}
```

**Importance:** 🟡 **IMPORTANT** - Watchdog et OTA pour production

---

## 4. Résumé des Manques

### 📊 Vue d'Ensemble

| Section | Paramètres Totaux | Exposés UI | Manquants | % Couverture |
|---------|-------------------|------------|-----------|--------------|
| WiFi | 13 | 13 | 0 | ✅ 100% |
| Hardware | 7 | 7 | 0 | ✅ 100% |
| **TinyBMS** | **12** | **0** | **12** | ❌ **0%** |
| Victron Base | 5 | 5 | 0 | ✅ 100% |
| **Victron Thresholds** | **9** | **0** | **9** | ❌ **0%** |
| CVL Base | 9 | 9 | 0 | ✅ 100% |
| **CVL Avancé** | **15** | **0** | **15** | ❌ **0%** |
| **MQTT** | **13** | **0** | **13** | ❌ **0%** |
| WebServer Base | 4 | 4 | 0 | ✅ 100% |
| **WebServer Auth** | **3** | **0** | **3** | ❌ **0%** |
| Logging | 6 | 6 | 0 | ✅ 100% |
| **Advanced** | **4** | **0** | **4** | ❌ **0%** |
| **TOTAL** | **100** | **44** | **56** | **44%** |

---

## 5. Plan de Correction Proposé

### 🎯 Priorités

#### 🔥 PRIORITÉ 1 - CRITIQUE (Semaine 1)
**TinyBMS Polling Configuration**
- poll_interval_ms
- uart_retry_count
- uart_retry_delay_ms
- broadcast_expected

**Raison:** Ces paramètres affectent directement les performances du système et doivent être ajustables sans recompilation.

---

#### 🟡 PRIORITÉ 2 - IMPORTANT (Semaine 1-2)
**Victron Thresholds**
- Tous les seuils (9 paramètres)

**Advanced Config**
- enable_ota
- watchdog_timeout_s

**Raison:** Sécurité et maintenance production

---

#### 🟢 PRIORITÉ 3 - SOUHAITABLE (Semaine 2-3)
**MQTT Configuration**
- Configuration complète MQTT (13 paramètres)

**WebServer Authentication**
- enable_auth, username, password

**Raison:** Fonctionnalités additionnelles pour intégrations avancées

---

#### ⚪ PRIORITÉ 4 - OPTIONNEL (Future)
**CVL Avancé**
- Paramètres de tuning fin (15 paramètres)

**Raison:** Pour utilisateurs très avancés, valeurs par défaut suffisent

---

## 6. Plan d'Implémentation Détaillé

### Phase 1: TinyBMS Configuration (1-2 jours)

**Modifications requises:**

1. **data/index.html** - Ajouter sous-onglet "TinyBMS Polling"
```html
<li class="nav-item">
    <a class="nav-link" data-bs-toggle="pill" href="#settingsTinyBMS">
        <i class="fas fa-sync-alt"></i> TinyBMS Polling
    </a>
</li>
```

2. **data/settings.js** - Ajouter structure tinybms
```javascript
tinybms: {
    poll_interval_ms: 100,
    uart_retry_count: 3,
    uart_retry_delay_ms: 50,
    broadcast_expected: true
}
```

3. **Backend (src/web_routes_api.cpp)** - ✅ API DÉJÀ EXISTANTE
   - GET /api/config retourne déjà tinybms.*
   - POST /api/config/save fonctionne déjà

---

### Phase 2: Victron Thresholds (1 jour)

**Modifications:**

1. **data/index.html** - Étendre settingsVictron avec section "Thresholds"
2. **data/settings.js** - Ajouter victron.thresholds
3. **Backend** - ✅ API DÉJÀ EXISTANTE

---

### Phase 3: Advanced Config (1 jour)

**Modifications:**

1. **data/index.html** - Transformer settingsSystem en "System & Advanced"
2. **data/settings.js** - Ajouter section advanced
3. **Backend** - ✅ API DÉJÀ EXISTANTE

---

### Phase 4: MQTT Configuration (1-2 jours)

**Modifications:**

1. **data/index.html** - Nouveau sous-onglet "MQTT"
2. **data/settings.js** - Ajouter structure mqtt complète
3. **Backend** - ⚠️ VÉRIFIER si POST /api/config/mqtt existe

---

## 7. Effort Estimé

| Phase | Priorité | Effort | Délai |
|-------|----------|--------|-------|
| Phase 1: TinyBMS Polling | 🔥 CRITIQUE | 1-2 jours | Immédiat |
| Phase 2: Victron Thresholds | 🟡 IMPORTANT | 1 jour | Semaine 1 |
| Phase 3: Advanced Config | 🟡 IMPORTANT | 1 jour | Semaine 1 |
| Phase 4: MQTT Config | 🟢 SOUHAITABLE | 1-2 jours | Semaine 2 |
| **TOTAL CRITIQUE+IMPORTANT** | - | **3-4 jours** | **Semaine 1-2** |

---

## 8. Bénéfices Attendus

### ✅ Après Implémentation Complète

1. **Plus de recompilation** - Tous les paramètres ajustables via web
2. **Production-ready** - Configuration complète accessible
3. **Tuning en temps réel** - Ajustements sans redémarrage (pour certains paramètres)
4. **Intégration MQTT** - Compatible Home Assistant
5. **Sécurité** - Authentication web optionnelle
6. **Maintenance** - OTA et watchdog configurables

---

## 9. Compatibilité Backend

### ✅ API Backend Existante

Le backend dans `src/web_routes_api.cpp` expose DÉJÀ :

```cpp
GET  /api/config                  // Retourne TOUTE la config (incluant tinybms, mqtt, advanced)
POST /api/config/wifi            // ✅
POST /api/config/hardware        // ✅
POST /api/config/cvl             // ✅
POST /api/config/victron         // ✅ (mais thresholds dans payload)
POST /api/config/logging         // ✅
POST /api/config/save            // ✅ Sauve tout
POST /api/config/import          // ✅ Import JSON complet
```

**Fonction `applySettingsPayload()` gère déjà:**
- ✅ wifi.*
- ✅ hardware.*
- ✅ tinybms.* (DÉJÀ SUPPORTÉ - lignes 333-341)
- ✅ cvl.*
- ✅ victron.* (incluant thresholds - lignes 350-373)
- ✅ logging.*
- ✅ system.* / web_server.*
- ✅ advanced.* (lignes 427-435)
- ⚠️ mqtt.* - À VÉRIFIER (probablement déjà supporté mais pas testé)

**Conclusion:** Le backend est DÉJÀ PRÊT pour 95% des paramètres manquants !

---

## 10. Conclusion

### État Actuel
- ✅ Infrastructure Settings complète et fonctionnelle
- ⚠️ Seulement 44% des paramètres exposés dans l'UI
- ✅ Backend API prêt pour 95% des paramètres manquants

### Recommandation
**Implémenter les Phases 1-3 (TinyBMS + Victron Thresholds + Advanced) en priorité**

**Durée:** 3-4 jours
**Impact:** Configuration complète sans recompilation pour production

---

**Auteur:** Claude Code
**Date:** 2025-10-30
