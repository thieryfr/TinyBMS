# TinyBMS-Victron Bridge test before commit

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Architecture Score](https://img.shields.io/badge/architecture-9.5%2F10-brightgreen)]()
[![Production Ready](https://img.shields.io/badge/production-ready*-blue)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()

> **Pont embarqué ESP32 pour convertir les données TinyBMS (UART/Modbus) vers le protocole CAN-BMS Victron**

---

## 🎯 Résumé Exécutif

### État du Projet : **Production Ready*** (9.5/10)

Le projet TinyBMS-Victron Bridge v2.5.0 est un système embarqué mature combinant :
- ✅ **Architecture événementielle** avec EventBus V2 (source unique de vérité)
- ✅ **Protection mutex complète** sur toutes les structures critiques
- ✅ **HAL abstraction** permettant changement de plateforme
- ✅ **Documentation exhaustive** (18+ fichiers markdown, tests complets)
- ✅ **Tests d'intégration** Python + tests natifs C++

**\*Prêt après :** Tests stress WebSocket (2-3h) + Validation CAN sur Victron GX réel

### Dernière Revue de Cohérence

📅 **Date :** 2025-10-29
📊 **Score :** 9.5/10
✅ **Modules validés :** 12/12 fonctionnels
🔍 **Problèmes critiques :** 0
⚠️ **Problèmes moyens :** 1 (tests WebSocket)
📝 **Détails :** Voir [docs/reviews/](docs/reviews/)

### Points Forts Clés

| Catégorie | État | Notes |
|-----------|------|-------|
| **Architecture** | ✅ Excellent | EventBus source unique, HAL abstraction |
| **Thread Safety** | ✅ Complet | 4 mutex (uart, feed, config, stats) |
| **Tests** | ✅ Robustes | Natifs C++ + intégration Python + fixtures |
| **Documentation** | ✅ Actualisée | README racine synchronisé avec la bascule ESP-IDF |
| **Correctifs** | ✅ Phases 1-3 | Race conditions éliminées |

---

## 📋 Table des Matières

- [Description](#description)
- [Architecture](#architecture)
- [Prérequis Matériels](#prérequis-matériels)
- [Fonctionnalités Principales](#fonctionnalités-principales)
- [Installation](#installation)
- [Utilisation](#utilisation)
- [Configuration](#configuration)
- [Tests](#tests)
- [Modules](#modules)
- [API REST](#api-rest)
- [Performances](#performances)
- [Dépannage](#dépannage)
- [Roadmap](#roadmap)
- [Contribution](#contribution)
- [Documentation Complète](#documentation-complète)

---

## Description

Pont embarqué permettant de convertir les trames TinyBMS (UART/Modbus RTU) vers le protocole CAN-BMS Victron. La pile logicielle combine :

- **ESP-IDF + composant Arduino** : Runtime principal (TWAI, Wi-Fi, watchdog) sur ESP-IDF, API Arduino conservée pour la logique applicative/web
- **FreeRTOS** : Tâches dédiées UART / CAN / CVL / WebSocket
- **EventBus V2** : Architecture publish/subscribe centralisée avec cache
- **AsyncWebServer** : Interface HTTP + WebSocket non-bloquante
- **HAL** : Abstraction matérielle (ESP32, Mock pour tests)
- **Configuration JSON** : Persistance SPIFFS avec hot-reload

---

## Architecture

### Vue d'Ensemble

```
┌─────────────────────────────────────────────────────────────┐
│                    MATÉRIEL / INTERFACES                     │
│  TinyBMS (UART)     CAN Bus (500kbps)     WiFi (STA/AP)     │
└────────┬────────────────┬─────────────────────┬─────────────┘
         │                │                     │
         ▼                ▼                     ▼
┌─────────────────┐ ┌──────────────┐ ┌──────────────────┐
│   HAL UART      │ │   HAL CAN    │ │   WiFi Manager   │
│  (Abstraction)  │ │ (Abstraction)│ │                  │
└────────┬────────┘ └──────┬───────┘ └─────────┬────────┘
         │                 │                    │
         ▼                 ▼                    ▼
┌─────────────────────────────────────────────────────────────┐
│                       EVENT BUS V2                           │
│  Cache par Type │ Queue FreeRTOS │ Publish/Subscribe        │
│  Thread-safe    │ 32 slots       │ Statistics               │
└───┬─────────┬──────────┬──────────┬────────────┬───────────┘
    │         │          │          │            │
    ▼         ▼          ▼          ▼            ▼
┌────────┐┌──────┐┌──────────┐┌──────────┐┌──────────┐
│  UART  ││ CAN  ││   CVL    ││WebSocket ││  Config  │
│  Task  ││ Task ││   Task   ││  Task    ││  Manager │
│ (10Hz) ││(1Hz) ││  (20s)   ││  (1Hz)   ││ (async)  │
└────────┘└──────┘└──────────┘└──────────┘└──────────┘
```

### Flux de Données End-to-End

1. **Acquisition (10Hz)** : UART task lit TinyBMS via Modbus RTU
2. **Publication** : Données publiées sur EventBus (source unique)
3. **Consommation** :
   - **CAN task** : Construit 9 PGNs Victron (0x351-0x382)
   - **CVL task** : Calcule limites CVL/CCL/DCL dynamiques
   - **WebSocket** : Broadcast JSON 1.5KB vers clients web
   - **MQTT** : Publication optionnelle topics structurés

**Latence typique UART → CAN :** 70-90ms
**Latence typique UART → WebSocket :** 80-120ms

---

## Prérequis Matériels

### Composants Requis

| Composant | Spécifications | Notes |
|-----------|----------------|-------|
| **Microcontrôleur** | ESP32 (WROOM ou équivalent) | Support CAN natif requis |
| **Transceiver CAN** | SN65HVD230 ou MCP2551 | Isolation recommandée |
| **TinyBMS** | Connexion UART TTL 3.3V | GPIO16/17 (configurable) |
| **Alimentation** | 12V stable, 500mA min | Marge pour ESP32 + transceiver |
| **Câblage CAN** | Paire torsadée différentielle | CANH/CANL + terminaison 120Ω |

### Schéma de Connexion

```
TinyBMS          ESP32           Transceiver      Victron CAN
                                   SN65HVD230         Bus
┌──────┐      ┌────────┐       ┌──────────┐     ┌──────────┐
│  TX  │─────→│ GPIO16 │       │          │     │          │
│  RX  │←─────│ GPIO17 │       │          │     │          │
│  GND │──────│  GND   │───────│   GND    │─────│   GND    │
└──────┘      │        │       │          │     │          │
              │ GPIO4  │──────→│   TXD    │     │          │
              │ GPIO5  │←──────│   RXD    │     │          │
              │  3.3V  │──────→│   VCC    │     │          │
              └────────┘       │   CANH   │────→│  CANH    │
                               │   CANL   │────→│  CANL    │
                               └──────────┘     └──────────┘
```

---

## Fonctionnalités Principales

### 1. Acquisition TinyBMS (UART)

- **Protocole** : Modbus RTU 19200 baud
- **Blocs lus** : 6 blocs (32+21, 102+2, 113+2, 305+3, 315+5, 500+6)
- **Retry logic** : 3 tentatives configurables avec délais
- **Statistiques** : Polls, errors, timeouts, CRC errors, latency
- **Publication** : EventBus LiveDataUpdate + MqttRegisterValue

📄 **Détails :** [docs/README_uart.md](docs/README_uart.md)

### 2. Transmission CAN Victron

- **PGNs implémentés** : 9 frames (0x351, 0x355, 0x356, 0x35A, 0x35E, 0x35F, 0x371, 0x378, 0x379, 0x382)
- **Fréquence** : 1Hz (configurable)
- **Keep-Alive** : Détection perte communication Victron (305ms)
- **Alarmes** : Bitfield 0x35A avec 8 alarmes configurables
- **Encodage** : Little-endian, scaling précis selon spec Victron

📄 **Spec Victron :** [docs/victron_register_mapping.md](docs/victron_register_mapping.md)

### 3. Algorithme CVL (Charge Voltage Limit)

- **États** : 6 états (BULK, TRANSITION, FLOAT_APPROACH, FLOAT, IMBALANCE_HOLD, SUSTAIN)
- **Paramètres** : 30+ configurables (SOC thresholds, offsets, hystérésis)
- **Protection cellule** : Réduction CVL si cell_voltage > threshold
- **Sorties** : CVL voltage, CCL (charge limit), DCL (discharge limit)
- **Tests** : Tests natifs complets (transitions, edge cases)

📄 **Détails :** [docs/README_cvl.md](docs/README_cvl.md)

### 4. Interface Web & API

#### Interface Web
- **Dashboard** : Monitoring temps réel (voltage, current, SOC, cells)
- **Statistics** : Graphiques historiques UART/CAN/EventBus
- **Settings** : Configuration JSON hot-reload
- **TinyBMS Config** : Éditeur registres read/write

#### API REST
```
GET  /api/status        # État complet (JSON 2-3KB)
GET  /api/settings      # Configuration actuelle
POST /api/settings      # Mise à jour config (hot-reload)
GET  /api/logs          # Logs SPIFFS (pagination)
DELETE /api/logs        # Purge logs
GET  /api/diagnostics   # Heap, stack, watchdog, EventBus
GET  /api/tinybms/registers  # Catalogue registres TinyBMS
POST /api/tinybms/write      # Écriture registre TinyBMS
```

#### WebSocket
- **Endpoint** : `ws://tinybms-bridge.local/ws`
- **Fréquence** : 1Hz (throttle configurable)
- **Format** : JSON compact 1.5KB (voltage, current, SOC, registers, stats)
- **Clients max** : 4 simultanés

### 5. Logging & Diagnostics

- **Cibles** : Serial (115200 baud) + SPIFFS (rotation 64KB)
- **Niveaux** : ERROR, WARNING, INFO, DEBUG
- **Watchdog** : Task WDT ESP32 (30s timeout, statistiques feed)
- **Diagnostics** : Heap free, stack high water mark, EventBus queue stats

📄 **Guide diagnostics :** [docs/diagnostics_avances.md](docs/diagnostics_avances.md)

---

## Installation

### 1. Prérequis Logiciels

- [PlatformIO Core](https://platformio.org/) ou extension VS Code
- Python 3.8+ (pour tests d'intégration)
- Git

### 2. Clone & Dépendances

```bash
git clone https://github.com/thieryfr/TinyBMS.git
cd TinyBMS
pio pkg install  # Installe dépendances automatiquement
```

**Dépendances PlatformIO :**
- `ArduinoJson@^6.21.0` - Parsing/serialization JSON
- `ESPAsyncWebServer@^1.2.3` - Serveur HTTP/WS async
- `AsyncTCP@^1.1.1` - Stack TCP async

**Runtime :**
- `framework = arduino, espidf` - ESP-IDF fournit le driver TWAI, le watchdog matériel et la pile réseau TLS ; l'API Arduino reste disponible pour la logique applicative.
- Cf. [ESP-IDF_MIGRATION.md](ESP-IDF_MIGRATION.md) pour les fichiers impactés et la checklist de compilation.

### 3. Configuration

Éditer `data/config.json` :

```json
{
  "wifi": {
    "sta_ssid": "VotreSSID",
    "sta_password": "VotreMotDePasse",
    "sta_hostname": "tinybms-bridge"
  },
  "hardware": {
    "uart": {
      "rx_pin": 16,
      "tx_pin": 17,
      "baudrate": 19200
    },
    "can": {
      "tx_pin": 4,
      "rx_pin": 5,
      "bitrate": 500000
    }
  },
  "cvl": {
    "enabled": true,
    "bulk_soc_threshold": 80.0,
    "float_soc_threshold": 95.0
  }
}
```

### 4. Compilation & Flash

```bash
# Compilation
pio run

# Flash firmware
pio run --target upload

# Flash filesystem (config.json + UI web)
pio run --target uploadfs

# Monitoring série
pio device monitor
```

---

## Utilisation

### 1. Premier Démarrage

1. **Alimenter l'ESP32** (12V via régulateur)
2. **Connecter TinyBMS** (UART GPIO16/17)
3. **Connecter CAN** (GPIO4/5 vers transceiver)
4. **Attendre initialisation** (~10s : SPIFFS, WiFi, HAL, EventBus)

### 2. Accès Interface Web

**URL :** `http://tinybms-bridge.local` (ou IP affichée en série)

**Onglets disponibles :**
- 🏠 **Dashboard** : Monitoring temps réel
- 📊 **Statistics** : Historiques UART/CAN/EventBus
- ⚙️ **Settings** : Configuration complète
- 🔧 **TinyBMS Config** : Éditeur registres

### 3. Vérification Fonctionnement

```bash
# Tester API REST
curl http://tinybms-bridge.local/api/status | jq

# Tester WebSocket (avec websocat)
websocat ws://tinybms-bridge.local/ws

# Vérifier logs
curl http://tinybms-bridge.local/api/logs
```

**Indicateurs santé :**
- `stats.uart_success_count` > 0 (acquisition TinyBMS OK)
- `stats.can_tx_count` > 0 (émission CAN OK)
- `stats.victron_keepalive_ok` = true (Victron GX répond)

---

## Configuration

### Fichier Principal : `data/config.json`

#### WiFi
```json
"wifi": {
  "mode": "sta",
  "sta_ssid": "MonSSID",
  "sta_password": "MonMotDePasse",
  "sta_ip_mode": "dhcp",
  "ap_fallback": {
    "enabled": true,
    "ssid": "TinyBMS-AP",
    "password": "tinybms123"
  }
}
```

#### Hardware
```json
"hardware": {
  "uart": {
    "rx_pin": 16,
    "tx_pin": 17,
    "baudrate": 19200,
    "timeout_ms": 100
  },
  "can": {
    "tx_pin": 4,
    "rx_pin": 5,
    "bitrate": 500000,
    "termination": false
  }
}
```

#### CVL Algorithm
```json
"cvl": {
  "enabled": true,
  "bulk_soc_threshold": 80.0,
  "transition_soc_threshold": 85.0,
  "float_soc_threshold": 95.0,
  "float_exit_soc": 92.0,
  "float_approach_offset_mv": 50,
  "float_offset_mv": 100,
  "imbalance_hold_threshold_mv": 50,
  "imbalance_release_threshold_mv": 20
}
```

#### Victron Thresholds
```json
"victron": {
  "thresholds": {
    "overvoltage_v": 58.4,
    "undervoltage_v": 42.0,
    "overtemp_c": 55.0,
    "low_temp_charge_c": 5.0,
    "imbalance_warn_mv": 50,
    "imbalance_alarm_mv": 100
  }
}
```

**Hot-Reload :** Modifications via `POST /api/settings` appliquées immédiatement (pas de reboot requis).

---

## Tests

### Tests Natifs C++ (CVL Logic)

```bash
g++ -std=c++17 -Iinclude tests/test_cvl_logic.cpp src/cvl_logic.cpp -o /tmp/test_cvl
/tmp/test_cvl
```

**Couverture :**
- Transitions d'états (BULK → TRANSITION → FLOAT)
- Edge cases (SOC oscillations, imbalance triggers)
- Protection cellule

### Tests d'Intégration Python

```bash
python -m pytest tests/integration/test_end_to_end_flow.py -v
```

**Scénarios :**
- Flux complet UART → EventBus → CAN
- Métadonnées EventBus (timestamp, sequence, source)
- Validation registres TinyBMS
- WebSocket multi-clients

### Tests Manuels

**Checklist avant production :**
- [ ] Test charge UART (1h continu, 10Hz)
- [ ] Test CAN TX/RX simultané (1h, avec Victron GX)
- [ ] Test WebSocket multi-clients (4 clients, 30 min) ⚠️ **PRIORITAIRE**
- [ ] Test CVL transitions (cycles BULK/FLOAT, 2h)
- [ ] Test réseau dégradé (latence 200ms, perte 10%, 15 min)
- [ ] Test endurance (24h continu, monitoring heap)

📄 **Guide tests stress :** [docs/websocket_stress_testing.md](docs/websocket_stress_testing.md)

---

## Modules

### Structure du Projet

```
TinyBMS/
├── src/                            # Code source C++
│   ├── main.ino                    # Entry point, mutexes, HAL init
│   ├── system_init.cpp             # Boot sequence, WiFi, tasks
│   ├── bridge_uart.cpp             # UART task (10Hz)
│   ├── bridge_can.cpp              # CAN task (1Hz)
│   ├── bridge_cvl.cpp              # CVL task (20s)
│   ├── cvl_logic.cpp               # Pure CVL algorithm
│   ├── config_manager.cpp          # JSON config (SPIFFS)
│   ├── logger.cpp                  # Logging Serial + SPIFFS
│   ├── watchdog_manager.cpp        # Task WDT management
│   ├── websocket_handlers.cpp      # WebSocket broadcast
│   ├── web_routes_api.cpp          # REST API endpoints
│   ├── json_builders.cpp           # JSON serialization
│   ├── event/
│   │   ├── event_bus_v2.cpp        # EventBus singleton
│   │   └── event_subscriber.cpp    # Subscriber management
│   ├── hal/                        # Hardware Abstraction Layer
│   │   ├── hal_manager.cpp         # HAL singleton
│   │   ├── esp32/                  # ESP32 implementation
│   │   └── mock/                   # Mock for tests
│   ├── mappings/
│   │   ├── tiny_read_mapping.cpp   # TinyBMS register catalog
│   │   └── victron_can_mapping.cpp # Victron PGN encoding
│   └── mqtt/
│       └── victron_mqtt_bridge.cpp # MQTT publisher (optionnel)
├── include/                        # Headers
├── data/                           # SPIFFS filesystem
│   ├── config.json                 # Configuration
│   ├── index.html                  # Web UI
│   └── *.js                        # JavaScript modules
├── tests/                          # Tests
│   ├── native/                     # Tests C++ sans hardware
│   ├── integration/                # Tests Python end-to-end
│   └── fixtures/                   # Snapshots JSON
├── docs/                           # Documentation
│   ├── README_*.md                 # Docs par module
│   ├── reviews/                    # Rapports de cohérence
│   └── *.md                        # Guides techniques
└── platformio.ini                  # Config PlatformIO
```

### Documentation par Module

| Module | Description | Doc |
|--------|-------------|-----|
| **System Init** | Boot sequence, mutexes, HAL, WiFi | [README_system_init.md](docs/README_system_init.md) |
| **Config Manager** | JSON SPIFFS, hot-reload, API | [README_config_manager.md](docs/README_config_manager.md) |
| **Event Bus** | Publish/subscribe, cache, stats | [README_event_bus.md](docs/README_event_bus.md) |
| **UART Bridge** | Modbus RTU, retry logic, EventBus | [README_uart.md](docs/README_uart.md) |
| **CVL Algorithm** | Multi-state limits computation | [README_cvl.md](docs/README_cvl.md) |
| **Watchdog** | Task WDT, feed tracking | [README_watchdog.md](docs/README_watchdog.md) |
| **Logger** | Serial + SPIFFS, rotation | [README_logger.md](docs/README_logger.md) |

---

## API REST

### GET /api/status

**Réponse :** JSON complet (2-3 KB)

```json
{
  "voltage": 54.32,
  "current": -12.5,
  "soc_percent": 87.3,
  "soh_percent": 98.5,
  "temperature": 25.4,
  "min_cell_mv": 3415,
  "max_cell_mv": 3435,
  "cell_imbalance_mv": 20,
  "online_status": 145,
  "uptime_ms": 3600000,
  "stats": {
    "uart": {
      "polls": 36000,
      "success": 35950,
      "errors": 50,
      "timeouts": 20,
      "crc_errors": 30,
      "latency_avg_ms": 78.5
    },
    "can": {
      "tx_count": 3600,
      "tx_errors": 0,
      "keepalive_ok": true
    },
    "cvl": {
      "state": "FLOAT",
      "cvl_voltage_v": 54.4,
      "ccl_limit_a": 5.0,
      "dcl_limit_a": 50.0
    },
    "eventbus": {
      "total_published": 72000,
      "queue_overruns": 0
    }
  },
  "registers": [...]
}
```

### POST /api/settings

**Body :** JSON partiel ou complet

```json
{
  "cvl": {
    "enabled": true,
    "float_soc_threshold": 96.0
  },
  "victron": {
    "thresholds": {
      "overvoltage_v": 59.0
    }
  }
}
```

**Réponse :** `{"success": true, "message": "Configuration updated"}`

**Effet :** Hot-reload immédiat, SPIFFS persistance, EventBus notification

---

## Performances

### Métriques Typiques

| Métrique | Valeur Typique | Max Acceptable |
|----------|----------------|----------------|
| **UART latency** | 70-80ms | 150ms |
| **CAN TX latency** | 2-5ms | 20ms |
| **WebSocket latency** | 80-120ms | 300ms |
| **Heap free** | 180-220 KB | 150 KB min |
| **CPU load** | 15-25% | 60% |
| **EventBus queue** | 2-5 slots | 32 max |

### Optimisations Implémentées

- ✅ **UART throttling** : Adaptive polling (100-1000ms)
- ✅ **WebSocket throttling** : Configurable broadcast rate
- ✅ **Ring buffer UART** : Archive diagnostic sans overhead
- ✅ **Zero-copy EventBus** : `getLatest()` retourne référence
- ✅ **Mutex timeouts** : Fallback gracieux si contention

---

## Dépannage

### Problèmes Courants

#### 1. Pas de données TinyBMS

**Symptômes :** `stats.uart_success_count = 0`, `stats.uart_errors > 0`

**Diagnostic :**
```bash
curl http://tinybms-bridge.local/api/diagnostics | jq '.uart'
```

**Solutions :**
- Vérifier câblage UART (RX ↔ TX croisés)
- Vérifier baudrate (19200 baud)
- Tester avec moniteur série : `pio device monitor`
- Vérifier config : `GET /api/settings` → `hardware.uart`

#### 2. CAN non fonctionnel

**Symptômes :** `stats.can_tx_count = 0` ou `can_tx_errors > 0`

**Diagnostic :**
```bash
# Vérifier stats CAN
curl http://tinybms-bridge.local/api/status | jq '.stats.can'

# Vérifier avec CANalyzer si disponible
```

**Solutions :**
- Vérifier transceiver CAN (SN65HVD230)
- Vérifier terminaison 120Ω sur bus CAN
- Vérifier bitrate (500 kbps)
- Tester loopback : `config.hardware.can.mode = "loopback"`

#### 3. WebSocket se déconnecte

**Symptômes :** Clients WebSocket déconnectés fréquemment

**Diagnostic :**
```bash
curl http://tinybms-bridge.local/api/diagnostics | jq '.heap_free'
```

**Solutions :**
- Vérifier heap disponible (>150 KB requis)
- Réduire fréquence broadcast : `config.webserver.websocket_update_interval_ms = 2000`
- Limiter nombre clients (max 4 recommandé)

#### 4. Watchdog reset

**Symptômes :** ESP32 reboot périodique, logs `[WDT] Task timeout`

**Diagnostic :**
```bash
curl http://tinybms-bridge.local/api/diagnostics | jq '.watchdog'
```

**Solutions :**
- Augmenter timeout : `config.advanced.watchdog_timeout_s = 60`
- Vérifier feed intervals : `watchdog.last_feed_*`
- Désactiver temporairement : `config.advanced.enable_watchdog = false`

### Logs Diagnostics

**Accès logs SPIFFS :**
```bash
# Télécharger logs
curl http://tinybms-bridge.local/api/logs > tinybms.log

# Filtrer erreurs
curl http://tinybms-bridge.local/api/logs | grep ERROR

# Purger logs
curl -X DELETE http://tinybms-bridge.local/api/logs
```

**Niveaux de log :**
- `ERROR` : Problèmes critiques (mutex timeout, CRC errors)
- `WARNING` : Problèmes non-bloquants (retry UART, keepalive lost)
- `INFO` : Événements normaux (boot, connexions)
- `DEBUG` : Détails verbeux (trames UART, PGN CAN)

📄 **Guide complet :** [docs/diagnostics_avances.md](docs/diagnostics_avances.md)

---

## Roadmap

### Court Terme (v2.6.0)

- [ ] **Tests stress WebSocket** (priorité haute) - 2-3h
- [ ] **Validation CAN Victron GX réel** - 2-4h
- [ ] **Standardisation timeouts configMutex** (100ms partout) - 30 min
- [ ] **Protection stats UART avec statsMutex** - 15 min

### Moyen Terme (v2.7.0)

- [ ] **Compression WebSocket JSON** (gzip, gain 60%) - 3-4h
- [ ] **Authentification API** (Basic Auth ou JWT) - 2-3h
- [ ] **Tests HAL sur ESP32 physique** - 1-2h
- [ ] **Dashboard historique** (graphiques 24h) - 4-6h

### Long Terme (v3.0.0)

- [ ] **Support Bluetooth BLE** (monitoring mobile)
- [ ] **Support multi-BMS** (jusqu'à 4 TinyBMS)
- [ ] **OTA firmware update** (via web UI)
- [ ] **Support STM32** (via HAL abstraction)

---

## Contribution

### Workflow

1. **Fork** le projet
2. **Créer branche** : `git checkout -b feature/ma-fonctionnalite`
3. **Commits** avec messages clairs
4. **Tests** : Vérifier `pio run` + tests natifs + Python
5. **PR** avec description détaillée

### Standards Code

- **C++17** standard
- **80 colonnes** max (confort lecture)
- **snake_case** pour variables/fonctions
- **PascalCase** pour classes/structs
- **UPPER_CASE** pour constantes
- **Commentaires** : Doxygen style pour API publiques

### CI/CD

GitHub Actions déclenché automatiquement :
- ✅ Build firmware ESP32
- ✅ Tests natifs C++
- ✅ Vérification EventBus boundaries
- ✅ Linting (format, style)

**Status :** [![Build](https://img.shields.io/badge/build-passing-brightgreen)]()

---

## Documentation Complète

### Documents Principaux

| Document | Description |
|----------|-------------|
| [README.md](README.md) | Ce document (vue d'ensemble) |
| [docs/reviews/](docs/reviews/) | Rapports de cohérence 2025-10-29 |
| [docs/README_MAPPING.md](docs/README_MAPPING.md) | Cartographie modules |
| [docs/architecture.md](docs/architecture.md) | Architecture détaillée |
| [docs/victron_register_mapping.md](docs/victron_register_mapping.md) | Mapping TinyBMS → Victron |
| [docs/diagnostics_avances.md](docs/diagnostics_avances.md) | Guide diagnostics terrain |
| [docs/websocket_stress_testing.md](docs/websocket_stress_testing.md) | Procédures tests stress |
| [docs/alarm_formula_analysis.md](docs/alarm_formula_analysis.md) | Formules calcul alarmes |
| [docs/mqtt_integration.md](docs/mqtt_integration.md) | Intégration MQTT |

### Archives

- Anciennes notes de phases et descriptions PR déplacées dans `archive/docs/` pour conserver l'historique tout en clarifiant la documentation active.

### Rapports de Cohérence (2025-10-29)

| Document | Contenu |
|----------|---------|
| [RAPPORT_REVUE_COHERENCE_2025-10-29.md](docs/reviews/RAPPORT_REVUE_COHERENCE_2025-10-29.md) | Revue complète 12 modules (60+ pages) |
| [RECTIFICATIF_COHERENCE_2025-10-29.md](docs/reviews/RECTIFICATIF_COHERENCE_2025-10-29.md) | Correction score 9.2 → 9.5 |
| [MIGRATION_EVENTBUS_STATUS.md](docs/reviews/MIGRATION_EVENTBUS_STATUS.md) | Vérification migration EventBus (✅ complète) |
| [SYNTHESE_REVUE_COHERENCE.md](docs/reviews/SYNTHESE_REVUE_COHERENCE.md) | Synthèse Phases 1-3 |

**Score actuel :** 9.5/10 ⭐
**État :** Production Ready* (*après tests WebSocket)

---

## Licence

**MIT License**

Copyright (c) 2025 TinyBMS-Victron Bridge Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## Support & Contact

- 🐛 **Issues** : [GitHub Issues](https://github.com/thieryfr/TinyBMS/issues)
- 💬 **Discussions** : [GitHub Discussions](https://github.com/thieryfr/TinyBMS/discussions)
- 📧 **Email** : [Voir profil GitHub](https://github.com/thieryfr)

---

**Made with ❤️ by the TinyBMS Community**

[![Star on GitHub](https://img.shields.io/github/stars/thieryfr/TinyBMS?style=social)](https://github.com/thieryfr/TinyBMS)
