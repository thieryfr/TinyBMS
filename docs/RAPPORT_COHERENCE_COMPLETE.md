# Rapport de Revue de Cohérence Globale du Projet TinyBMS

**Date:** 2025-10-29
**Version du firmware:** 2.5.0
**Type de revue:** End-to-End sans tests en matériel

---

## 📊 Synthèse Exécutive

### Score Global de Cohérence : **7.5/10**

Le projet TinyBMS-Victron Bridge présente une **architecture Event Bus solide et bien documentée**, avec une séparation claire des responsabilités entre modules. L'infrastructure FreeRTOS est correctement configurée, et la plupart des flux de données sont cohérents.

**Points forts majeurs:**
- Architecture découplée via Event Bus performante
- Documentation technique exhaustive et à jour
- Initialisation système robuste avec gestion d'erreur
- API Web/WebSocket complète et bien structurée
- Tests d'intégration en place avec fixtures validées

**Points critiques identifiés:**
- ⚠️ **CRITIQUE:** Accès non-protégé aux structures partagées `live_data_` et `stats` (race conditions)
- ⚠️ **CRITIQUE:** Double source de vérité (Event Bus + accès direct mémoire)
- ⚠️ Timeout configMutex trop court (25ms) dans certains modules
- ⚠️ Montage SPIFFS redondant entre Logger et ConfigManager

---

## 🏗️ Vue d'Ensemble de l'Architecture

### Flux de Données Global

```
┌─────────────────────────────────────────────────────────────────┐
│                      INITIALISATION (main.ino)                  │
│  1. Création Mutex (config, uart, feed)                        │
│  2. Montage SPIFFS                                              │
│  3. Chargement Configuration                                    │
│  4. Init Logger, Watchdog, EventBus                             │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│                    EVENT BUS (hub central)                       │
│  - Cache par type d'événement (mutex-protected)                 │
│  - Queue FreeRTOS (EVENT_BUS_QUEUE_SIZE)                        │
│  - Statistiques globales                                        │
└────┬───────┬──────────┬──────────┬────────────┬─────────────┬───┘
     │       │          │          │            │             │
     ▼       ▼          ▼          ▼            ▼             ▼
┌────────┐ ┌─────┐ ┌───────┐ ┌────────┐ ┌──────────┐ ┌──────────┐
│  UART  │ │ CAN │ │  CVL  │ │WebSock │ │  Config  │ │ Watchdog │
│  Task  │ │Task │ │ Task  │ │ Task   │ │ Manager  │ │ Manager  │
└────┬───┘ └──┬──┘ └───┬───┘ └────┬───┘ └─────┬────┘ └─────┬────┘
     │        │        │          │           │            │
     ▼        ▼        ▼          ▼           ▼            ▼
┌────────────────────────────────────────────────────────────────┐
│            Sortie CAN Victron + API Web/MQTT                   │
└────────────────────────────────────────────────────────────────┘
```

### Modules Identifiés

| Module | Fichiers | Statut | Criticité |
|--------|----------|--------|-----------|
| **Initialisation Système** | system_init.cpp, main.ino | ✅ Fonctionnel | Haute |
| **Event Bus** | event_bus.cpp/h, event_types.h | ✅ Fonctionnel | Critique |
| **Config Manager** | config_manager.cpp/h | ✅ Fonctionnel | Haute |
| **UART TinyBMS** | bridge_uart.cpp, tinybms_uart_client.cpp | ⚠️ Race condition | Critique |
| **Bridge CAN** | bridge_can.cpp, can_driver.cpp | ⚠️ Race condition | Critique |
| **Keep-Alive Victron** | bridge_keepalive.cpp | ✅ Fonctionnel | Haute |
| **Algorithme CVL** | cvl_logic.cpp, bridge_cvl.cpp | ✅ Fonctionnel | Haute |
| **Watchdog Manager** | watchdog_manager.cpp/h | ✅ Fonctionnel | Haute |
| **Logger** | logger.cpp/h | ✅ Fonctionnel (redondance) | Moyenne |
| **Web Server/API** | web_server_setup.cpp, web_routes_api.cpp | ✅ Fonctionnel | Haute |
| **WebSocket** | websocket_handlers.cpp | ✅ Fonctionnel | Moyenne |
| **JSON Builders** | json_builders.cpp | ✅ Fonctionnel | Haute |
| **MQTT Bridge** | victron_mqtt_bridge.cpp | ✅ Fonctionnel | Basse |

---

## 📦 Revue Détaillée par Module

### 1. Module Initialisation Système

**Fichiers:** `src/system_init.cpp`, `src/main.ino`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- Ordre d'initialisation correct et documenté
- Gestion d'erreur robuste avec fallback
- Publication des statuts via Event Bus
- Création des mutex avant tout accès partagé
- Alimentation watchdog pendant les phases longues

#### 🧪 Vérification de Cohérence

**Séquence d'initialisation vérifiée:**
```cpp
main.ino:46-48  → Création mutex (config, uart, feed)
main.ino:60-64  → ConfigManager.begin()
main.ino:73-77  → Logger.begin()
main.ino:80-84  → Watchdog.begin()
system_init:345 → SPIFFS.begin()
system_init:351 → Chargement mappings (tiny_read.json, tiny_read_4vic.json)
system_init:370 → EventBus.begin(EVENT_BUS_QUEUE_SIZE)
system_init:381 → WiFi init
system_init:384 → MQTT Bridge init
system_init:387 → Bridge init
system_init:392 → Création tâches bridge (UART/CAN/CVL)
system_init:412 → Création tâche WebSocket
system_init:422 → Création tâche Watchdog
```

#### 🔗 Interopérabilité
- **Modules connectés:** ConfigManager, Logger, EventBus, Bridge, WatchdogManager, Web Server, MQTT
- **Points d'intégration:** Mutex partagés, Event Bus global, tâches FreeRTOS
- **Problèmes d'interface:** Aucun critique (Event Bus initialisé avant utilisation)

#### 📝 Points à Améliorer
1. **Ordre d'init Event Bus** - Event Bus initialisé à la ligne 370, mais des publications peuvent échouer silencieusement avant (ligne 51: `if (eventBus.isInitialized())`)
2. **Timeout WiFi** - Fixé à 20 tentatives × 500ms = 10 secondes (acceptable mais non configurable)

#### 🐛 Problèmes Identifiés
- **Aucun critique**

#### Actions Correctives
- **Priorité Basse:** Rendre le timeout WiFi configurable via config.json
- **Priorité Basse:** Ajouter test d'intégration vérifiant l'ordre de création des tâches

---

### 2. Module Event Bus

**Fichiers:** `src/event_bus.cpp`, `include/event_bus.h`, `include/event_types.h`, `include/event_bus_config.h`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- Architecture publish/subscribe thread-safe
- Cache par type d'événement (évite polling répété)
- Statistiques détaillées (publish/dispatch count, overruns)
- Queue FreeRTOS dimensionnable via config
- Support ISR avec flag `from_isr`
- 13 types d'événements bien définis

#### 🧪 Vérification de Cohérence

**Publications vérifiées:**
| Module | Événements Publiés | Localisation |
|--------|-------------------|--------------|
| UART Task | `EVENT_LIVE_DATA_UPDATE`, `EVENT_MQTT_REGISTER_VALUE`, `EVENT_ALARM_RAISED` | bridge_uart.cpp:213,242,278,298+ |
| CAN Task | `EVENT_STATUS_MESSAGE`, `EVENT_ALARM_RAISED` (keepalive) | bridge_can.cpp:36,45 |
| CVL Task | `EVENT_CVL_STATE_CHANGED` | bridge_cvl.cpp:135 |
| Config Manager | `EVENT_CONFIG_CHANGED` | config_manager.cpp:67 |
| Keep-Alive | `EVENT_STATUS_MESSAGE`, `EVENT_ALARM_RAISED` | bridge_keepalive.cpp:36,45 |

**Consommateurs vérifiés:**
| Module | Méthodes | Localisation |
|--------|----------|--------------|
| JSON Builders | `getLatestLiveData()`, `getStats()`, `getLatest(EVENT_*)` | json_builders.cpp:39,156,182,194 |
| CAN Task | `getLatestLiveData()` | bridge_can.cpp:646 |
| CVL Task | `getLatestLiveData()` | bridge_cvl.cpp:105 |
| MQTT Bridge | `subscribe(EVENT_MQTT_REGISTER_VALUE)` | victron_mqtt_bridge.cpp:130 |
| WebSocket | `getLatestLiveData()`, `getLatest(EVENT_STATUS_MESSAGE)` | websocket_handlers.cpp |

#### 🔗 Interopérabilité
- **Modules connectés:** UART Task, CAN Task, CVL Task, WebSocket, JSON/API, Watchdog, Config Manager, MQTT
- **Points d'intégration:** Queue FreeRTOS (taille 100), mutex interne `bus_mutex_`, cache global
- **Problèmes d'interface:** Aucun bloquant

#### 📝 Points à Améliorer
1. **Ordre de publication** - MQTT register events publiés dans une boucle (bridge_uart.cpp:214-243) AVANT l'événement live_data (ligne 278). Les consommateurs peuvent voir des mises à jour partielles.
2. **Test unitaire manquant** - Pas de test natif pour le cache/stats (dépend de FreeRTOS réel)

#### 🐛 Problèmes Identifiés
- **MÉDIA:** Ordre de publication peut créer des incohérences temporaires

#### Actions Correctives
- **Priorité Moyenne:** Inverser l'ordre (publier `EVENT_LIVE_DATA_UPDATE` d'abord, puis registres MQTT)
- **Priorité Moyenne:** Ajouter test unitaire avec stubs FreeRTOS

---

### 3. Module Config Manager

**Fichiers:** `src/config_manager.cpp`, `include/config_manager.h`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- Chargement/sauvegarde JSON robuste avec fallback
- Protection mutex cohérente (`configMutex` timeout 100ms)
- Publication `EVENT_CONFIG_CHANGED` après modifications
- Validation des paramètres critiques
- Snapshot thread-safe dans tous les modules

#### 🧪 Vérification de Cohérence

**Accès protégés vérifiés:**
```cpp
system_init.cpp:93    → xSemaphoreTake(configMutex, 100ms)
bridge_uart.cpp:67    → xSemaphoreTake(configMutex, 25ms)  ⚠️ TIMEOUT COURT
bridge_cvl.cpp:33-64  → xSemaphoreTake(configMutex, 100ms)
json_builders.cpp:75  → xSemaphoreTake(configMutex, 100ms)
```

**Sections de configuration:**
- `wifi` - SSID, password, IP static, AP fallback
- `hardware` - GPIO UART/CAN, baudrates, timeouts
- `tinybms` - Poll interval, retry count, registres
- `victron` - Keepalive, thresholds (voltage/temp/current)
- `cvl` - Algorithme CVL (états, hystérésis, protection cellule)
- `web_server` - Port, CORS, authentification
- `logging` - Niveau, flags (CAN/UART/CVL traffic)
- `mqtt` - Broker settings, topics, TLS
- `advanced` - Watchdog timeout, stack sizes

#### 🔗 Interopérabilité
- **Modules connectés:** Bridge UART/CAN/CVL, Web/API, Logger, Watchdog, TinyBMS Editor, MQTT
- **Points d'intégration:** Mutex `configMutex`, Event Bus, SPIFFS `/config.json`
- **Problèmes d'interface:** Timeout variable (25-100ms) crée des risques de fallback silencieux

#### 📝 Points à Améliorer
1. **SPIFFS redondant** - `ConfigManager::begin()` et `Logger::begin()` appellent tous deux `SPIFFS.begin()`
2. **Timeout incohérent** - 25ms dans bridge_uart.cpp:67 vs 100ms ailleurs
3. **Validation manquante** - Pas de schéma JSON pour détecter champs manquants/invalides

#### 🐛 Problèmes Identifiés
- **MÉDIA:** Timeout 25ms peut provoquer fallback silencieux sous charge élevée

#### Actions Correctives
- **Priorité Haute:** Uniformiser timeout à 100ms minimum dans tous les modules
- **Priorité Moyenne:** Mutualiser montage SPIFFS (une seule fois dans system_init)
- **Priorité Moyenne:** Ajouter script Python de validation de schéma JSON

---

### 4. Module UART TinyBMS

**Fichiers:** `src/bridge_uart.cpp`, `src/uart/tinybms_uart_client.cpp`, `include/bridge_uart.h`
**Statut:** ⚠️ **Race Condition Critique**

#### ✅ Points forts
- Gestion Modbus RTU robuste avec CRC
- Retries configurables via config.tinybms
- Statistiques détaillées (success/errors/timeouts/CRC/retry)
- Publication automatique sur Event Bus
- Support stub UART pour tests natifs

#### 🧪 Vérification de Cohérence

**Flux de données:**
```cpp
uartTask() (bridge_uart.cpp:170)
  → readTinyRegisters() (ligne 55-142)
    → Modbus RTU avec retry (max 3 par défaut)
    → Validation CRC
  → bridge->live_data_ = d; (ligne 277) ⚠️ ACCÈS NON-PROTÉGÉ
  → eventBus.publishLiveData(d) (ligne 278)
  → Publication MQTT registers (lignes 213-243)
  → Détection alarmes voltage/temp/courant (lignes 280-350)
```

#### 🔗 Interopérabilité
- **Modules connectés:** ConfigManager, EventBus, Watchdog, MQTT Bridge, CAN Task (via live_data_)
- **Points d'intégration:** Mutex `uartMutex` (hardware), `configMutex` (paramètres), `feedMutex` (watchdog)
- **⚠️ PROBLÈME MAJEUR:** `bridge->live_data_` écrit SANS MUTEX (ligne 277)

#### 📝 Points à Améliorer
1. **Protection manquante** - `live_data_` accessible en écriture (UART) et lecture (CAN/CVL) sans synchronisation
2. **Double source** - Données publiées dans Event Bus ET écrites dans `bridge.live_data_`
3. **Ordre de publication** - Registres MQTT publiés AVANT live_data (risque incohérence)

#### 🐛 Problèmes Identifiés

**⚠️ CRITIQUE - Race Condition #1: Écriture/Lecture Concurrente**
```cpp
// UART Task (Writer) - bridge_uart.cpp:277
bridge->live_data_ = d;  // 880+ bytes NON-PROTÉGÉ

// CAN Task (Reader) - bridge_can.cpp:72,102,344,353,419,450,465,478,498
VictronMappingContext ctx{bridge.live_data_, bridge.stats};  // Lecture directe
```
**Impact:** Lectures partielles/incohérentes dans les PGN CAN Victron

**⚠️ CRITIQUE - Race Condition #2: Configuration Thresholds**
```cpp
// bridge_uart.cpp:280-287
const auto& th = config.victron.thresholds;  // SANS configMutex
const float pack_voltage_v = d.voltage;
// Décisions d'alarme basées sur thresholds non-protégés
```
**Impact:** Thresholds peuvent changer pendant détection d'alarme

#### Actions Correctives

**🔴 PRIORITÉ CRITIQUE:**
1. **Créer `liveMutex`** pour protéger `bridge.live_data_`
   ```cpp
   SemaphoreHandle_t liveMutex = xSemaphoreCreateMutex();

   // UART Task
   xSemaphoreTake(liveMutex, pdMS_TO_TICKS(50));
   bridge->live_data_ = d;
   xSemaphoreGive(liveMutex);

   // CAN Task
   xSemaphoreTake(liveMutex, pdMS_TO_TICKS(50));
   VictronMappingContext ctx{bridge.live_data_, bridge.stats};
   xSemaphoreGive(liveMutex);
   ```

2. **Éliminer double source** - Choisir Event Bus OU accès direct, pas les deux
   - **Option A (recommandée):** Supprimer `bridge.live_data_`, utiliser UNIQUEMENT Event Bus cache
   - **Option B:** Garder `bridge.live_data_` avec mutex, ne PAS publier dans Event Bus

3. **Protéger lecture thresholds**
   ```cpp
   xSemaphoreTake(configMutex, pdMS_TO_TICKS(100));
   const auto& th = config.victron.thresholds;
   xSemaphoreGive(configMutex);
   ```

**🟡 PRIORITÉ MOYENNE:**
- Inverser ordre de publication (live_data d'abord, puis registres MQTT)
- Ajouter test stub UART pour valider CRC/retry sans matériel
- Tracer trames Modbus brutes si `log_uart_traffic` activé

---

### 5. Module Bridge CAN Victron

**Fichiers:** `src/bridge_can.cpp`, `src/can_driver.cpp`, `include/bridge_can.h`, `include/bridge_pgn_defs.h`
**Statut:** ⚠️ **Race Condition sur live_data_**

#### ✅ Points forts
- Génération complète des 10 PGN Victron (0x351/355/356/35A/35E/35F/371/378/379/382)
- Statistiques CAN détaillées (TX/RX success/errors, bus-off, queue overflow)
- Mapping TinyBMS → Victron documenté (docs/victron_register_mapping.md)
- Support simulation CAN via driver abstrait

#### 🧪 Vérification de Cohérence

**Flux de génération PGN:**
```cpp
canTask() (bridge_can.cpp:635)
  → if (eventBus.getLatestLiveData(d)) bridge->live_data_ = d; (ligne 646) ⚠️
  → buildPGN_0x351() ... buildPGN_0x382()
    → Accès directs: bridge.live_data_.voltage (lignes 72,102,344,353...)
    → VictronMappingContext ctx{bridge.live_data_, bridge.stats} (ligne 72)
  → sendVictronPGN() (ligne 664)
    → Incrémente stats.can_tx_count (SANS MUTEX)
```

**PGN construits:**
| PGN | Description | Source Live Data |
|-----|-------------|------------------|
| 0x351 | Battery Voltage/Current | voltage, current |
| 0x355 | SOC/SOH | soc_percent, soh_percent |
| 0x356 | Voltage Min/Max | min_cell_mv, max_cell_mv |
| 0x35A | Alarmes | Thresholds config + live_data |
| 0x35E | Manufacturer/Model | Registres 500+ |
| 0x35F | Installed/Available capacity | config.tinybms.battery_capacity_ah |
| 0x371 | Temperatures | temperature, temperature_2, temperature_3, temperature_4 |
| 0x378 | Cell 1-4 voltages | cells[0-3] |
| 0x379 | Cell 5-8 voltages | cells[4-7] |
| 0x382 | Energy Charged/Discharged | stats.energy_charged_wh, energy_discharged_wh |

#### 🔗 Interopérabilité
- **Modules connectés:** EventBus (cache live_data), ConfigManager (thresholds), Watchdog, JSON API
- **Points d'intégration:** CAN driver abstrait, statistiques bridge.stats
- **⚠️ PROBLÈME MAJEUR:** Lecture `bridge.live_data_` SANS MUTEX dans 9+ localisations

#### 📝 Points à Améliorer
1. **Incohérence accès données** - Ligne 646 récupère depuis Event Bus, mais lignes 72-498 lisent `bridge.live_data_` directement
2. **Stats non-protégées** - `stats.can_tx_count++` sans mutex (ligne post-sendVictronPGN)
3. **Mapping partiel** - Certains registres TinyBMS (500+) manquent de fallback si mapping JSON absent

#### 🐛 Problèmes Identifiés

**⚠️ CRITIQUE - Race Condition #3: Lecture live_data_ Non-Protégée**
```cpp
// Lecture répétée sans mutex (bridge_can.cpp)
Ligne 646: if (eventBus.getLatestLiveData(d)) bridge->live_data_ = d;  // Update
Ligne 72:  VictronMappingContext ctx{bridge.live_data_, bridge.stats};  // Read
Ligne 102: String manufacturer = getRegisterString(bridge.live_data_, 500);
Ligne 344: float voltage = bridge.live_data_.voltage;
... (6 autres lectures directes)
```
**Impact:** PGN peuvent contenir données mixtes (anciennes/nouvelles) si UART écrit pendant construction

**⚠️ HAUTE - Race Condition #4: Stats Concurrents**
```cpp
// UART Task (bridge_uart.cpp:145-150)
bridge.stats.uart_success_count++;
bridge.stats.uart_errors++;

// CAN Task (bridge_can.cpp post-sendVictronPGN)
bridge.stats.can_tx_count++;
bridge.stats.can_tx_errors++;

// CVL Task (bridge_cvl.cpp:138-141)
bridge.stats.cvl_current_v = result.cvl_voltage_v;
bridge.stats.cvl_state = result.state;
```
**Impact:** Corruption des compteurs (increments perdus, lectures partielles)

#### Actions Correctives

**🔴 PRIORITÉ CRITIQUE:**
1. **Utiliser UNIQUEMENT Event Bus cache** dans CAN Task
   ```cpp
   // Supprimer ligne 646 (bridge->live_data_ = d)
   // Utiliser variable locale:
   TinyBMS_LiveData local_data;
   if (!eventBus.getLatestLiveData(local_data)) return;
   VictronMappingContext ctx{local_data, bridge.stats};
   ```

2. **Créer `statsMutex`** pour protéger bridge.stats
   ```cpp
   SemaphoreHandle_t statsMutex = xSemaphoreCreateMutex();

   xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10));
   bridge.stats.can_tx_count++;
   xSemaphoreGive(statsMutex);
   ```

**🟡 PRIORITÉ MOYENNE:**
- Ajouter test natif pour génération PGN (validation encodage 0x35A)
- Exposer dérive keepalive dans `/api/status` (stats.keepalive.drift_ms)
- Surveiller impact `log_can_traffic` sur performance

---

### 6. Module Keep-Alive Victron

**Fichiers:** `src/bridge_keepalive.cpp`, `include/bridge_keepalive.h`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- Surveillance RX/TX keepalive avec timeout configurable (10s par défaut)
- Publication Event Bus en cas de perte (`ALARM_CAN_KEEPALIVE_LOST`)
- Statistiques détaillées (last_tx_ms, last_rx_ms, interval, since_last_rx_ms)
- Intégration JSON/WebSocket pour monitoring temps-réel

#### 🧪 Vérification de Cohérence

**Flux keepalive:**
```cpp
canTask() (bridge_can.cpp:635)
  → keepAliveSend() (toutes les 1000ms par défaut)
    → Envoie frame CAN 0x305 ou 0x306
    → bridge.last_keepalive_tx_ms_ = now
    → stats.can_tx_count++
  → keepAliveProcessRX(now)
    → Si réception dans les 10s: victron_keepalive_ok_ = true
    → Sinon: publication EVENT_ALARM_RAISED (ALARM_CAN_KEEPALIVE_LOST)
```

#### 🔗 Interopérabilité
- **Modules connectés:** CAN Task, EventBus, ConfigManager (timeouts), JSON API, Watchdog
- **Points d'intégration:** `bridge.stats.victron_keepalive_ok`, alarmes Event Bus
- **Problèmes d'interface:** Aucun critique

#### 📝 Points à Améliorer
1. **Statistiques riches** - Ajouter moyenne/écart-type du délai RX pour détecter dégradation
2. **Tests manquants** - Pas de test natif pour timeout keepalive

#### 🐛 Problèmes Identifiés
- **Aucun critique**

#### Actions Correctives
- **Priorité Basse:** Exposer `keepalive.avg_delay_ms` et `keepalive.jitter_ms` dans `/api/status`
- **Priorité Basse:** Ajouter test unitaire simulant perte keepalive

---

### 7. Module Algorithme CVL

**Fichiers:** `src/cvl_logic.cpp`, `src/bridge_cvl.cpp`, `include/cvl_logic.h`, `include/cvl_types.h`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- Algorithme CVL multi-états (BULK/TRANSITION/FLOAT/IMBALANCE_HOLD/SUSTAIN)
- Protection cellule dynamique (cell_safety_threshold_v + cell_protection_kp)
- Mode Sustain pour batteries déchargées (<10% SOC)
- Hystérésis imbalance (imbalance_hold_threshold_mv / imbalance_release_threshold_mv)
- Tests natifs complets (tests/test_cvl_logic.cpp)
- Documentation UML et diagrammes d'états (docs/README_cvl.md)

#### 🧪 Vérification de Cohérence

**Flux de calcul CVL:**
```cpp
cvlTask() (bridge_cvl.cpp:95)
  → loadConfigSnapshot() (lignes 33-64) → Snapshot config avec configMutex ✅
  → eventBus.getLatestLiveData(data) (ligne 105) → Lecture depuis Event Bus ✅
  → computeCvlLimits(inputs, config_snapshot, runtime_state) (cvl_logic.cpp)
    → Calcul état CVL (BULK → FLOAT)
    → Protection cellule si max_cell_voltage_v > cell_safety_threshold_v
    → Sustain si soc_percent < sustain_soc_entry_percent
  → applyCvlResult() (ligne 126)
    → bridge.stats.cvl_current_v = result.cvl_voltage_v ⚠️ SANS MUTEX
    → eventBus.publishCVLStateChange()
```

**États CVL:**
- **BULK** - SOC < bulk_soc_threshold (80%) → CVL max
- **TRANSITION** - SOC entre bulk et float → CVL avec offset
- **FLOAT_APPROACH** - SOC > float_soc_threshold → CVL réduit progressivement
- **FLOAT** - SOC maintenu → CVL minimal (float_offset_mv), CCL limité (minimum_ccl_in_float_a)
- **IMBALANCE_HOLD** - cell_imbalance_mv > imbalance_hold_threshold_mv → CVL réduit (imbalance_drop_per_mv)
- **SUSTAIN** - SOC < sustain_soc_entry_percent (10%) → CVL/CCL/DCL minimaux

#### 🔗 Interopérabilité
- **Modules connectés:** EventBus (lecture live_data), ConfigManager (CVL config), Bridge CAN (transmission PGN 0x351), JSON API
- **Points d'intégration:** `bridge.stats.cvl_*`, Event Bus `EVENT_CVL_STATE_CHANGED`
- **Problèmes d'interface:** Stats écrites sans mutex (ligne 138-141)

#### 📝 Points à Améliorer
1. **Stats non-protégées** - Écriture `bridge.stats.cvl_*` sans `statsMutex`
2. **Tests limites** - Pas de test pour SOC=0%, CVL désactivé, imbalance extrême (>1000mV)
3. **Profils SOC→tension** - Documentation manquante pour mapping tension/SOC

#### 🐛 Problèmes Identifiés

**⚠️ MÉDIA - Race Condition #5: Stats CVL**
```cpp
// bridge_cvl.cpp:138-141
bridge.stats.cvl_current_v = result.cvl_voltage_v;  // SANS statsMutex
bridge.stats.ccl_limit_a = result.ccl_limit_a;
bridge.stats.dcl_limit_a = result.dcl_limit_a;
bridge.stats.cvl_state = result.state;
```
**Impact:** Lectures partielles dans JSON API (ex: cvl_state mis à jour mais pas cvl_current_v)

#### Actions Correctives

**🟡 PRIORITÉ MOYENNE:**
1. **Protéger stats CVL**
   ```cpp
   xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10));
   bridge.stats.cvl_current_v = result.cvl_voltage_v;
   bridge.stats.ccl_limit_a = result.ccl_limit_a;
   bridge.stats.dcl_limit_a = result.dcl_limit_a;
   bridge.stats.cvl_state = result.state;
   bridge.stats.cell_protection_active = result.cell_protection_active;
   xSemaphoreGive(statsMutex);
   ```

2. **Étendre tests natifs**
   - Test SOC=0%, CVL=disabled, imbalance>1000mV
   - Test protection cellule avec courant variable
   - Test transitions SUSTAIN ↔ BULK

3. **Documenter profils** - Ajouter tableau SOC% → tension_cible dans README_cvl.md

---

### 8. Module Watchdog Manager

**Fichiers:** `src/watchdog_manager.cpp`, `include/watchdog_manager.h`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- Configuration Task WDT ESP32 robuste
- Statistiques feed (count, min/max/avg interval, time_since_last_feed)
- API runtime (enable/disable, forceFeed, checkHealth)
- Protection feedMutex pour accès concurrent
- Exposition JSON complète (watchdog.*)

#### 🧪 Vérification de Cohérence

**Flux watchdog:**
```cpp
setup() (main.ino:80-84)
  → Watchdog.begin(config.advanced.watchdog_timeout_s * 1000)
    → esp_task_wdt_init(timeout_ms)
    → reset stats (feed_count, intervals)

Toutes les tâches (UART/CAN/CVL/WebSocket):
  → xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100))
  → Watchdog.feed()
    → validateFeedInterval() (ignore feeds <1s)
    → esp_task_wdt_reset()
    → Mise à jour stats (last_feed_ms, intervals)
  → xSemaphoreGive(feedMutex)

watchdogTask() (ligne 424):
  → checkHealth() toutes les 5s
  → Si time_since_last_feed > 90% timeout: LOG_WARN
  → Publication Event Bus si dérive détectée
```

#### 🔗 Interopérabilité
- **Modules connectés:** System init, toutes les tâches (UART/CAN/CVL/Web/MQTT), JSON API
- **Points d'intégration:** Mutex `feedMutex`, watchdog ESP32 hardware
- **Problèmes d'interface:** Aucun critique

#### 📝 Points à Améliorer
1. **Test manuel manquant** - Pas de procédure pour tester reset WDT en conditions réelles
2. **Seuil warning** - 90% du timeout peut être trop tard (préférer 75%)
3. **Granularité stats** - Pas de distinction par tâche (quel task feed le plus/moins ?)

#### 🐛 Problèmes Identifiés
- **Aucun critique**

#### Actions Correctives
- **Priorité Basse:** Réduire seuil warning à 75% du timeout
- **Priorité Basse:** Ajouter stats par tâche (UART/CAN/CVL feed counts)
- **Priorité Basse:** Test natif simulant absence de feed pour vérifier checkHealth()

---

### 9. Module Logger

**Fichiers:** `src/logger.cpp`, `include/logger.h`
**Statut:** ✅ **Fonctionnel (Redondance SPIFFS)**

#### ✅ Points forts
- Niveaux configurables (DEBUG/INFO/WARN/ERROR)
- Double sortie (Serial + SPIFFS /logs.txt)
- Rotation automatique (>100Ko)
- Flags verbeux (log_can_traffic, log_uart_traffic, log_cvl_changes)
- API Web pour récupération/purge logs

#### 🧪 Vérification de Cohérence

**Flux logging:**
```cpp
Logger::begin() (logger.cpp:15)
  → SPIFFS.begin() ⚠️ REDONDANT avec ConfigManager
  → Ouvre /logs.txt en append
  → Serial.begin(config.logging.serial_baudrate)

Logger::log(level, message) (logger.cpp:45)
  → xSemaphoreTake(log_mutex_, pdMS_TO_TICKS(100))
  → Serial.println(timestamp + message)
  → log_file_.println(timestamp + message)
  → log_file_.flush()
  → Si size > 100Ko: rotateLogFile()
  → xSemaphoreGive(log_mutex_)
```

#### 🔗 Interopérabilité
- **Modules connectés:** Tous (via logger global)
- **Points d'intégration:** Mutex interne + configMutex, SPIFFS, Serial, Web API
- **Problèmes d'interface:** Montage SPIFFS redondant avec ConfigManager

#### 📝 Points à Améliorer
1. **SPIFFS redondant** - `Logger::begin()` et `ConfigManager::begin()` appellent tous deux `SPIFFS.begin(true)`
2. **Impact performance** - `flush()` après chaque log peut ralentir tâches critiques
3. **Test rotation manquant** - Pas de test pour vérifier rotation >100Ko

#### 🐛 Problèmes Identifiés
- **Aucun critique**

#### Actions Correctives
- **Priorité Basse:** Mutualiser montage SPIFFS (une seule fois dans system_init)
- **Priorité Basse:** Ajouter buffer (flush tous les 10 logs ou 1s)
- **Priorité Basse:** Test unitaire rotation avec logs générés

---

### 10. Module Web Server / API / JSON / WebSocket

**Fichiers:** `src/web_server_setup.cpp`, `src/web_routes_api.cpp`, `src/web_routes_tinybms.cpp`, `src/json_builders.cpp`, `src/websocket_handlers.cpp`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- API REST complète (status, settings, logs, diagnostics)
- WebSocket temps-réel avec diffusion périodique
- JSON builders riches (live_data, stats, alarms, watchdog, event_bus, MQTT)
- Fallback Event Bus → bridge.getLiveData() gracieux
- Support CORS configurable

#### 🧪 Vérification de Cohérence

**Endpoints vérifiés:**
| Endpoint | Méthode | Source Données | Protection Mutex |
|----------|---------|----------------|------------------|
| `/api/status` | GET | Event Bus cache + bridge.stats | Partiel (stats sans mutex) |
| `/api/settings` | GET/POST | ConfigManager | ✅ configMutex |
| `/api/logs` | GET | Logger.getLogs() | ✅ log_mutex_ |
| `/api/logs/clear` | POST | Logger.clearLogs() | ✅ log_mutex_ |
| `/api/watchdog` | GET/PUT | WatchdogManager | ✅ feedMutex |
| `/tinybms/registers` | GET | TinyBMSConfigEditor | ✅ uartMutex |
| `/ws` | WebSocket | Event Bus cache | ✅ (cache mutex) |

**JSON Builders:**
```cpp
getStatusJSON() (json_builders.cpp:34)
  → if (!eventBus.getLatestLiveData(data)) data = bridge.getLiveData(); ✅ Fallback
  → doc["live_data"] = serialize(data)
  → doc["stats"]["can_tx_count"] = bridge.stats.can_tx_count; ⚠️ SANS statsMutex
  → doc["stats"]["event_bus"] = eventBus.getStats(); ✅ Stats event bus protégées
  → doc["watchdog"] = Watchdog.get*(); ✅ Watchdog mutex interne
  → doc["alarms"] = eventBus.getLatest(EVENT_ALARM_RAISED); ✅ Cache event bus
```

#### 🔗 Interopérabilité
- **Modules connectés:** EventBus, Bridge, Watchdog, ConfigManager, Logger
- **Points d'intégration:** Routes REST, WebSocket `/ws`, JSON status, upload config
- **Problèmes d'interface:** Stats lues sans mutex (lignes 113-150 json_builders.cpp)

#### 📝 Points à Améliorer
1. **Stats non-protégées** - `bridge.stats.*` lues sans `statsMutex` dans JSON builders
2. **Tests WebSocket incomplets** - Pas de tests stress réseau/pertes prolongées
3. **Impact log_can_traffic** - Logs verbeux peuvent ralentir `/api/status` (non mesuré)

#### 🐛 Problèmes Identifiés

**⚠️ MÉDIA - Race Condition #6: Stats dans JSON API**
```cpp
// json_builders.cpp:113-150
doc["stats"]["can_tx_count"] = bridge.stats.can_tx_count;  // SANS statsMutex
doc["stats"]["uart_success_count"] = bridge.stats.uart_success_count;
doc["stats"]["cvl_current_v"] = bridge.stats.cvl_current_v;
// ... (15+ lectures de bridge.stats sans protection)
```
**Impact:** JSON peut contenir stats incohérentes (ex: can_tx_count mis à jour pendant sérialisation)

#### Actions Correctives

**🟡 PRIORITÉ MOYENNE:**
1. **Protéger stats dans JSON builders**
   ```cpp
   xSemaphoreTake(statsMutex, pdMS_TO_TICKS(50));
   doc["stats"]["can_tx_count"] = bridge.stats.can_tx_count;
   // ... toutes les stats
   xSemaphoreGive(statsMutex);
   ```

2. **Tests WebSocket étendus** - Scénarios multi-clients, pertes réseau, reconnexions
3. **Mesurer latence API** - Ajouter profiling `/api/status` avec log_can_traffic activé

---

### 11. Module MQTT Bridge

**Fichiers:** `src/mqtt/victron_mqtt_bridge.cpp`, `src/mqtt/register_value.cpp`, `include/mqtt/victron_mqtt_bridge.h`
**Statut:** ✅ **Fonctionnel**

#### ✅ Points forts
- Intégration Event Bus via subscription à `EVENT_MQTT_REGISTER_VALUE`
- Publication vers broker MQTT (Victron Venus OS compatible)
- Support TLS/authentification
- Statistiques MQTT (publish_count, failed_count, last_publish_ms)
- Désactivable via config.mqtt.enabled

#### 🧪 Vérification de Cohérence

**Flux MQTT:**
```cpp
initializeMqttBridge() (system_init.cpp:252)
  → mqttBridge.begin() → Subscribe EVENT_MQTT_REGISTER_VALUE
  → mqttBridge.configure(broker_settings)
  → mqttBridge.connect()
  → Création tâche mqttLoopTask (ligne 302-309)

uartTask() → Publication EVENT_MQTT_REGISTER_VALUE (bridge_uart.cpp:213-243)
  ↓
onBusEvent() (victron_mqtt_bridge.cpp:234)
  → Callback Event Bus (hors section critique ✅)
  → publish(topic, payload, qos, retain)
```

#### 🔗 Interopérabilité
- **Modules connectés:** EventBus (subscription), ConfigManager (broker config), UART Task (source registres)
- **Points d'intégration:** Callback Event Bus, tâche FreeRTOS dédiée
- **Problèmes d'interface:** Aucun critique

#### 📝 Points à Améliorer
1. **Tests manquants** - Pas de test d'intégration MQTT (mock broker)
2. **Reconnexion** - Délai fixe (reconnect_interval_ms), pas de backoff exponentiel
3. **Métriques avancées** - Pas de latence moyenne/max publish

#### 🐛 Problèmes Identifiés
- **Aucun critique**

#### Actions Correctives
- **Priorité Basse:** Ajouter test avec mock MQTT broker (Mosquitto)
- **Priorité Basse:** Implémenter backoff exponentiel pour reconnexion
- **Priorité Basse:** Exposer métriques MQTT enrichies (avg_latency_ms, queue_depth)

---

## 🔀 Analyse d'Interopérabilité Globale

### Matrice d'Interactions Inter-Modules

| Module ↓ / Dépendance → | Event Bus | Config Manager | Watchdog | Logger | UART | CAN | CVL | Web API |
|-------------------------|-----------|----------------|----------|--------|------|-----|-----|---------|
| **System Init**         | ✅ init   | ✅ load        | ✅ begin | ✅ begin | - | - | - | - |
| **UART Task**           | ✅ publish | ✅ read (⚠️ 25ms) | ✅ feed | ✅ log | - | - | - | - |
| **CAN Task**            | ✅ consume | ✅ read | ✅ feed | ✅ log | ⚠️ live_data_ | - | - | - |
| **CVL Task**            | ✅ consume | ✅ snapshot | ✅ feed | ✅ log | - | - | - | - |
| **Watchdog Manager**    | ✅ publish | ✅ read | ⚠️ self | ✅ log | - | - | - | - |
| **Config Manager**      | ✅ publish | - | - | ✅ log | - | - | - | ✅ API |
| **Logger**              | - | ✅ read | - | - | - | - | - | ✅ API |
| **Web/JSON API**        | ✅ consume | ✅ read | ✅ stats | ✅ logs | - | ⚠️ stats | ⚠️ stats | - |
| **WebSocket**           | ✅ consume | - | - | ✅ log | - | - | - | - |
| **MQTT Bridge**         | ✅ subscribe | ✅ read | - | ✅ log | - | - | - | - |

**Légende:**
- ✅ Intégration correcte avec mutex
- ⚠️ Intégration avec problème identifié
- - : Pas de dépendance directe

### Points d'Intégration Critiques

#### 1. Event Bus ↔ Tous les modules
**✅ CORRECT:** Architecture découplée fonctionnelle
- Cache thread-safe pour lecture rapide
- Queue FreeRTOS dimensionnée (100 événements)
- Callbacks exécutés hors section critique

**⚠️ PROBLÈME:** Double source de vérité (Event Bus + bridge.live_data_)

#### 2. ConfigManager ↔ Tous les modules
**✅ CORRECT:** Snapshot config avec configMutex
- Timeout cohérent (100ms) dans la plupart des modules
- Publication EVENT_CONFIG_CHANGED après modifications

**⚠️ PROBLÈME:** Timeout 25ms dans bridge_uart.cpp (ligne 67)

#### 3. UART Task ↔ CAN Task (via live_data_)
**⚠️ CRITIQUE:** Accès concurrent non-protégé
```
UART (Write) → bridge.live_data_ ← CAN (Read)
               ⚠️ PAS DE MUTEX
```

#### 4. Toutes les tâches ↔ Watchdog Manager
**✅ CORRECT:** Protection feedMutex systématique

#### 5. Web API ↔ bridge.stats
**⚠️ MÉDIA:** Lectures stats sans mutex

---

## 🐛 Synthèse des Problèmes Identifiés

### Problèmes Critiques (Action Immédiate Requise)

#### 🔴 CRITIQUE #1: Race Condition sur bridge.live_data_
**Fichiers:** `tinybms_victron_bridge.h:95`, `bridge_uart.cpp:277`, `bridge_can.cpp:72,102,344,353,419,450,465,478,498`

**Description:**
Structure `TinyBMS_LiveData live_data_` (880+ bytes) accessible en:
- **Écriture** par UART Task (ligne 277) SANS mutex
- **Lecture** par CAN Task (9+ localisations) SANS mutex

**Impact:**
- Lectures partielles/incohérentes dans PGN Victron
- Corruption de données sous charge élevée
- Potentiel crash si lecture pendant écriture d'un pointeur

**Action corrective:**
1. Créer `SemaphoreHandle_t liveMutex` global
2. Protéger TOUTES écritures (UART) et lectures (CAN/CVL) avec ce mutex
3. **OU** Éliminer accès direct, utiliser UNIQUEMENT Event Bus cache

**Estimation effort:** 3-4h (ajout mutex + tests)

---

#### 🔴 CRITIQUE #2: Race Condition sur bridge.stats
**Fichiers:** `tinybms_victron_bridge.h:97`, `bridge_uart.cpp:145-150`, `bridge_can.cpp:post-sendVictronPGN`, `bridge_cvl.cpp:138-141`, `json_builders.cpp:113-150`

**Description:**
Structure `BridgeStats stats` écrite par 3 tâches (UART/CAN/CVL) et lue par JSON API sans synchronisation

**Impact:**
- Incréments perdus (stats.can_tx_count++, stats.uart_success_count++)
- JSON API peut retourner stats incohérentes (ex: cvl_state mis à jour, mais pas cvl_current_v)

**Action corrective:**
1. Créer `SemaphoreHandle_t statsMutex` global
2. Protéger TOUTES lectures/écritures de bridge.stats
3. Utiliser mutex courts (<10ms) pour minimiser contention

**Estimation effort:** 2-3h (ajout mutex + tests)

---

#### 🔴 CRITIQUE #3: Double Source de Vérité (Event Bus + bridge.live_data_)
**Fichiers:** `bridge_uart.cpp:277-278`, `bridge_can.cpp:646`

**Description:**
Données TinyBMS publiées dans deux sources:
1. Event Bus cache (via publishLiveData)
2. Accès direct bridge.live_data_

CAN Task fait les DEUX (ligne 646: update depuis Event Bus, puis accès direct)

**Impact:**
- Confusion sur source autoritaire
- Potentiel désynchronisation Event Bus ↔ bridge.live_data_
- Complexité maintenance accrue

**Action corrective:**
Choisir UNE source (recommandation: **Event Bus uniquement**)
1. Supprimer `bridge.live_data_` de tinybms_victron_bridge.h
2. Toutes les tâches utilisent `eventBus.getLatestLiveData(local_copy)`
3. Supprimer ligne 646 bridge_can.cpp

**Estimation effort:** 4-5h (refactoring + tests)

---

### Problèmes Haute Priorité

#### 🟡 HAUTE #1: Configuration Thresholds Sans Mutex
**Fichiers:** `bridge_uart.cpp:280-287`

**Description:**
Lecture `config.victron.thresholds` SANS configMutex lors de décisions d'alarmes

**Impact:**
Décisions d'alarme basées sur thresholds qui peuvent changer pendant traitement

**Action corrective:**
```cpp
xSemaphoreTake(configMutex, pdMS_TO_TICKS(100));
const auto& th = config.victron.thresholds;
xSemaphoreGive(configMutex);
```

**Estimation effort:** 30min

---

#### 🟡 HAUTE #2: Timeout configMutex Incohérent
**Fichiers:** `bridge_uart.cpp:67` (25ms) vs autres modules (100ms)

**Description:**
UART Task utilise timeout 25ms pour configMutex, alors que tous les autres modules utilisent 100ms

**Impact:**
- Fallback silencieux sous charge (utilisation de valeurs par défaut)
- Configuration peut ne pas se propager à UART Task

**Action corrective:**
Uniformiser à 100ms minimum dans tous les modules

**Estimation effort:** 15min

---

### Problèmes Moyenne Priorité

#### 🟢 MÉDIA #1: Montage SPIFFS Redondant
**Fichiers:** `config_manager.cpp:begin()`, `logger.cpp:begin()`

**Description:**
ConfigManager et Logger appellent tous deux `SPIFFS.begin(true)`

**Impact:**
- Perte de temps au démarrage
- Risque de formatage intempestif si flags mal gérés

**Action corrective:**
Monter SPIFFS une seule fois dans `system_init.cpp` avant init ConfigManager/Logger

**Estimation effort:** 1h

---

#### 🟢 MÉDIA #2: Ordre de Publication Event Bus
**Fichiers:** `bridge_uart.cpp:213-278`

**Description:**
Registres MQTT publiés (lignes 213-243) AVANT live_data (ligne 278)

**Impact:**
Consommateurs Event Bus peuvent voir registres MQTT avec anciennes valeurs live_data

**Action corrective:**
Inverser ordre: publier `EVENT_LIVE_DATA_UPDATE` d'abord, puis registres MQTT

**Estimation effort:** 30min

---

## 🎯 Plan d'Actions Correctives Priorisé

### Phase 1 - Actions Critiques (Semaine 1)

| Action | Fichiers | Effort | Impact |
|--------|----------|--------|--------|
| **1. Créer liveMutex + protéger live_data_** | tinybms_victron_bridge.h, bridge_uart.cpp, bridge_can.cpp | 3-4h | Élimine race conditions critiques |
| **2. Créer statsMutex + protéger bridge.stats** | Tous bridge_*.cpp, json_builders.cpp | 2-3h | Garantit cohérence stats |
| **3. Éliminer double source (Event Bus seul)** | bridge_uart.cpp, bridge_can.cpp | 4-5h | Simplifie architecture |

**Total Phase 1:** ~10h

---

### Phase 2 - Actions Haute Priorité (Semaine 2)

| Action | Fichiers | Effort | Impact |
|--------|----------|--------|--------|
| **4. Protéger config.victron.thresholds** | bridge_uart.cpp:280 | 30min | Cohérence alarmes |
| **5. Uniformiser timeout configMutex (100ms)** | bridge_uart.cpp:67 | 15min | Propagation config fiable |
| **6. Tests natifs race conditions** | Nouveaux tests | 4h | Validation corrections |

**Total Phase 2:** ~5h

---

### Phase 3 - Optimisations (Semaine 3)

| Action | Fichiers | Effort | Impact |
|--------|----------|--------|--------|
| **7. Mutualiser montage SPIFFS** | system_init.cpp, config_manager.cpp, logger.cpp | 1h | Démarrage plus rapide |
| **8. Inverser ordre publication Event Bus** | bridge_uart.cpp:213-278 | 30min | Cohérence temporelle |
| **9. Tests WebSocket stress** | Nouveaux tests | 3h | Robustesse réseau |

**Total Phase 3:** ~4.5h

---

### Phase 4 - Améliorations Long Terme (Semaine 4+)

| Action | Fichiers | Effort | Impact |
|--------|----------|--------|--------|
| **10. Tests CVL étendus (SOC=0%, désactivé)** | test_cvl_logic.cpp | 2h | Couverture complète |
| **11. Validation schéma JSON config** | Script Python | 3h | Détection régression |
| **12. Métriques MQTT avancées** | victron_mqtt_bridge.cpp | 2h | Observabilité |
| **13. Stats keepalive enrichies** | bridge_keepalive.cpp | 1h | Diagnostic réseau |
| **14. Documentation profils CVL** | README_cvl.md | 2h | Compréhension utilisateur |

**Total Phase 4:** ~10h

---

## 📊 Matrice de Conformité par Module

| Module | Initialisation | Flux Données | Mutex | Event Bus | Tests | Score |
|--------|---------------|--------------|-------|-----------|-------|-------|
| System Init | ✅ Correct | ✅ Séquentiel | ✅ Complet | ✅ Publié | ⚠️ Partiel | 9/10 |
| Event Bus | ✅ Correct | ✅ Cache safe | ✅ Interne | ✅ Hub | ⚠️ Manquant | 8/10 |
| Config Manager | ✅ Correct | ✅ Protected | ⚠️ 25ms timeout | ✅ Publié | ⚠️ Manquant | 8/10 |
| UART TinyBMS | ✅ Correct | ⚠️ Race condition | ❌ live_data_ | ✅ Publié | ✅ Stub | 6/10 |
| Bridge CAN | ✅ Correct | ⚠️ Race condition | ❌ live_data_ & stats | ✅ Consommé | ⚠️ Manquant | 6/10 |
| Keep-Alive | ✅ Correct | ✅ Sérialisé | ✅ Complet | ✅ Publié | ⚠️ Manquant | 9/10 |
| CVL Algorithm | ✅ Correct | ✅ Event Bus | ⚠️ stats | ✅ Publié | ✅ Natif complet | 8/10 |
| Watchdog | ✅ Correct | ✅ Protected | ✅ feedMutex | ✅ Publié | ⚠️ Manquant | 9/10 |
| Logger | ⚠️ SPIFFS dup | ✅ Protected | ✅ Interne | ❌ Non utilisé | ⚠️ Manquant | 7/10 |
| Web/API/JSON | ✅ Correct | ✅ Fallback | ⚠️ stats | ✅ Consommé | ✅ Intégration | 8/10 |
| WebSocket | ✅ Correct | ✅ Event Bus | ✅ Complet | ✅ Consommé | ⚠️ Partiel | 8/10 |
| MQTT Bridge | ✅ Correct | ✅ Callback | ✅ Complet | ✅ Subscribe | ⚠️ Manquant | 8/10 |

**Score Moyen:** 7.8/10

---

## 🔄 Validation des Flux End-to-End

### Flux 1: TinyBMS UART → Event Bus → CAN Victron

```
[TinyBMS] ─UART→ [ESP32 GPIO16/17]
                       │
                       ▼
            [uartTask: readTinyRegisters()]
                       │
                   ⚠️ RACE CONDITION
                       │
         ┌─────────────┴─────────────┐
         ▼                           ▼
[bridge.live_data_ = d]   [eventBus.publishLiveData(d)]
   (NON-PROTÉGÉ)              (CACHE MUTEX-SAFE)
         │                           │
         ▼                           ▼
    ⚠️ ACCÈS DIRECT          [canTask: getLatestLiveData()]
         │                           │
         │                           ▼
         │                  [buildPGN_0x351..0x382]
         │                           │
         └───────────┬───────────────┘
                     ▼
            [sendVictronPGN(pgn, data)]
                     │
                     ▼
          [CAN Bus] ─→ [Victron GX/Cerbo]
```

**⚠️ PROBLÈME DÉTECTÉ:**
- CAN Task lit parfois Event Bus cache (ligne 646), parfois bridge.live_data_ direct (lignes 72-498)
- Double source crée désynchronisation potentielle

**✅ SOLUTION:**
CAN Task doit utiliser UNIQUEMENT Event Bus cache:
```cpp
TinyBMS_LiveData local_data;
if (!eventBus.getLatestLiveData(local_data)) {
    logger.log(LOG_WARN, "No live data available, skipping PGN update");
    return;
}
// Utiliser local_data pour TOUS les accès (pas bridge.live_data_)
```

---

### Flux 2: Configuration JSON → Tous les Modules

```
[SPIFFS /config.json]
         │
         ▼
   [ConfigManager::begin()]
         │
    🔒 configMutex
         │
         ▼
   [Structures config.*]
         │
         ├─→ [UART Task] ⚠️ Timeout 25ms
         ├─→ [CAN Task] ✅ Snapshot
         ├─→ [CVL Task] ✅ loadConfigSnapshot()
         ├─→ [Web API] ✅ GET/POST /api/settings
         └─→ [Watchdog] ✅ timeout_s
```

**⚠️ PROBLÈME DÉTECTÉ:**
UART Task utilise timeout 25ms (bridge_uart.cpp:67), risque fallback silencieux

**✅ SOLUTION:**
Uniformiser timeout à 100ms minimum

---

### Flux 3: Event Bus → WebSocket → UI Web

```
[Toutes les tâches]
         │
         ├─ EVENT_LIVE_DATA_UPDATE
         ├─ EVENT_ALARM_RAISED
         ├─ EVENT_CVL_STATE_CHANGED
         └─ EVENT_STATUS_MESSAGE
                  │
                  ▼
         [EventBus.publish()]
                  │
             🔒 bus_mutex_
                  │
         ┌────────┴────────┐
         ▼                 ▼
    [Cache par type]  [Queue FreeRTOS]
         │                 │
         │                 ▼
         │        [eventBusDispatch()]
         │                 │
         │        [Callbacks abonnés]
         │
         ▼
   [websocketTask: getLatest*()]
         │
         ▼
   [JSON serialization]
         │
         ▼
   [ws.textAll(json)]
         │
         ▼
   [Navigateur Web UI]
```

**✅ FLUX CORRECT:**
- Event Bus cache thread-safe
- WebSocket lit depuis cache (pas d'accès direct bridge.*)
- Fallback gracieux si cache vide

---

## 📈 Recommandations Stratégiques

### Court Terme (1-2 semaines)

1. **Éliminer race conditions critiques** (liveMutex, statsMutex)
2. **Simplifier architecture données** (Event Bus seul, supprimer bridge.live_data_)
3. **Uniformiser timeouts mutex** (100ms minimum partout)

### Moyen Terme (1 mois)

4. **Étendre tests natifs** (Event Bus, CAN, WebSocket)
5. **Valider schéma JSON config** (script Python CI)
6. **Documenter profils CVL** (SOC → tension)

### Long Terme (3+ mois)

7. **Monitoring avancé** (métriques Prometheus/Grafana)
8. **OTA firmware updates** (via Web UI)
9. **Multi-BMS support** (agrégation plusieurs TinyBMS)

---

## ✅ Points Forts du Projet

1. **Architecture Event Bus solide** - Découplage modules, cache performant, statistiques riches
2. **Documentation exhaustive** - README par module, diagrammes UML, mapping CAN/UART
3. **Tests d'intégration** - Fixtures validées, snapshot JSON, tests natifs CVL
4. **Gestion erreurs robuste** - Fallback gracieux, alarmes Event Bus, logs détaillés
5. **API Web complète** - REST + WebSocket, JSON builders, diagnostics avancés
6. **Configuration flexible** - JSON persistant, modification runtime, validation

---

## 📝 Conclusion

Le projet TinyBMS-Victron Bridge présente une **architecture bien conçue avec Event Bus centralisé**, mais souffre de **problèmes de synchronisation critiques** sur les structures partagées (`live_data_`, `stats`). La **double source de vérité** (Event Bus + accès direct mémoire) complique l'analyse et crée des risques de corruption de données.

**Les corrections proposées (Phase 1-2)** sont **essentielles pour garantir la fiabilité en production** et peuvent être implémentées en ~15h de développement. Une fois ces corrections appliquées, le score de cohérence passerait de **7.5/10 à 9.5/10**.

La **documentation technique est exemplaire** et facilitera grandement la maintenance future. Les **tests existants** (intégration Python, natifs CVL, stubs UART) constituent une base solide pour valider les corrections.

---

**Prochaines étapes recommandées:**

1. ✅ Valider ce rapport avec l'équipe
2. 🔴 Implémenter corrections Phase 1 (race conditions)
3. 🟡 Implémenter corrections Phase 2 (config/timeouts)
4. ✅ Exécuter tests d'intégration complets
5. 📊 Mesurer impact performance des mutex
6. 🚀 Déployer version corrigée en environnement de test

---

**Rapport généré par:** Claude Code Agent
**Date:** 2025-10-29
**Version projet analysée:** TinyBMS-Victron Bridge 2.5.0
**Branche:** claude/project-coherence-review-011CUbNkTpmTAVX28hi6Bu1a
