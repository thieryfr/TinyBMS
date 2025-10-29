# TinyBMS-Victron Bridge

## Description
Pont embarqué permettant de convertir les trames TinyBMS (UART/Modbus) vers le protocole CAN-BMS Victron. La pile logicielle combine FreeRTOS (tâches dédiées UART / CAN / CVL / Web), un Event Bus centralisé avec cache et statistiques, un serveur asynchrone (HTTP + WebSocket) et un système de configuration JSON persisté sur SPIFFS.

## Table des matières
- [Description](#description)
- [Structure du Projet](#structure-du-projet)
- [Prérequis matériels](#prérequis-matériels)
- [Fonctionnalités principales](#fonctionnalités-principales)
- [Flux de données](#flux-de-données)
- [Modules](#modules)
- [Installation](#installation)
- [Utilisation](#utilisation)
- [Tests](#tests)
- [Dépannage CI](#dépannage-ci)
- [Plan d'actions prioritaire](#plan-dactions-prioritaire)
- [Notes de développement](#notes-de-développement)

## Structure du Projet
```
TinyBMS/
├── README.md                       # Présent document
├── README_system_init.md           # Module initialisation
├── README_config_manager.md        # Module configuration JSON
├── README_event_bus.md             # Bus d'événements
├── README_uart.md                  # Acquisition TinyBMS UART
├── README_cvl.md                   # Algorithme CVL
├── README_watchdog.md              # Gestion watchdog
├── README_logger.md                # Journalisation
├── docs/                            # Documentation technique & bilans de revue
│   ├── REVUE_MODULES.md            # Rapport de cohérence détaillé
│   └── diagnostics_avances.md      # Guide de diagnostics terrain (watchdog/CAN/UART, SPIFFS)
├── data/                           # Contenu SPIFFS (config + UI)
├── include/                        # Headers partagés (API modules)
├── src/                            # Implémentations C++/INO
├── tests/                          # Tests natifs & intégration
├── scripts/                        # Outils de déploiement
├── platformio.ini                  # Configuration PlatformIO
└── partitions.csv                  # Partitionnement ESP32
```

## Prérequis matériels
- **ESP32** avec support CAN natif (ex. ESP32 WROOM + transceiver SN65HVD230).
- **TinyBMS** connecté en UART (niveau TTL 3.3 V) sur GPIO16/17.
- **Bus CAN Victron** : câblage différentiel CANH/CANL via transceiver isolé recommandé.
- **Alimentation stable 12 V** : prévoir une marge pour l'ESP32, le transceiver CAN et le TinyBMS.
- **Interface de configuration** : ordinateur avec USB-UART pour le premier flash et l'accès console.

## Fonctionnalités principales
- **Acquisition TinyBMS** : tâche UART avec retries configurables, statistiques détaillées et publication automatique sur l'Event Bus (`src/bridge_uart.cpp`).
- **Publication Victron CAN-BMS** : génération des PGN 0x351/0x355/0x356/0x35A/0x35E/0x35F/0x371/0x378/0x379/0x382, supervision keep-alive et alarmes configurables (`src/bridge_can.cpp`, `src/bridge_keepalive.cpp`).
- **Algorithme CVL évolué** : calcul dynamique CVL/CCL/DCL multi-états avec hystérésis et événements dédiés (`src/cvl_logic.cpp`, `src/bridge_cvl.cpp`).
- **Event Bus** : singleton FreeRTOS publish/subscribe avec cache par type, statistiques et diffusion WebSocket (`src/event_bus.cpp`).
- **Configuration JSON** : chargement/sauvegarde SPIFFS, instantanés protégés par mutex et API REST pour édition (`src/config_manager.cpp`, `src/web_routes_api.cpp`).
- **Supervision Web** : serveur HTTP/WS asynchrone, builders JSON riches et diffusion WebSocket (`src/web_server_setup.cpp`, `src/json_builders.cpp`, `src/websocket_handlers.cpp`).
- **Watchdog & journalisation** : gestion centralisée Task WDT + logs Serial/SPIFFS avec rotation (`src/watchdog_manager.cpp`, `src/logger.cpp`).

## Flux de données
1. **Acquisition** : la tâche UART récupère en continu les mesures TinyBMS et les publie sur l'Event Bus.
2. **Normalisation** : les données entrantes sont enrichies (unités, bornes, alarmes) par `cvl_logic` et les builders JSON.
3. **Diffusion** :
   - vers le bus **CAN Victron** via `bridge_can` et `bridge_keepalive` ;
   - vers l'interface **HTTP/WebSocket** pour la supervision distante ;
   - vers les **journalisations SPIFFS** pour audit ultérieur.
4. **Configuration** : les changements reçus via l'API REST sont validés, persistés dans SPIFFS et rediffusés sur l'Event Bus.

### Diagramme de flux global
```
┌──────────────────┐
│   Boot ESP32 &   │
│  system_init.cpp │
│  (SPIFFS, WiFi,  │
│  tâches FreeRTOS)│
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Chargement JSON  │
│  ConfigManager   │
│  (SPIFFS → RAM)  │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│  EventBus global │
│ (init + queue    │
│  FreeRTOS)       │
└──────┬─┬─┬───────┘
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       │ │ │
       ▼ ▼ ▼
┌──────────────┐   ┌────────────────┐   ┌────────────────────┐
│ bridge_uart  │   │  cvl_logic &   │   │  bridge_can +      │
│ (TinyBMS →   │   │  bridge_cvl    │   │  bridge_keepalive  │
│  LiveData)   │   │ (normalisation │   │  (PGN Victron)     │
└──────┬───────┘   │  limites CVL)  │   └────────┬──────────┘
       │           └──────┬─────────┘            │
       │                  │                      │
       │                  │                      ▼
       │                  │            ┌────────────────────┐
       │                  │            │  can_driver.cpp    │
       │                  │            │  (bus CAN physique)│
       │                  │            └────────────────────┘
       │                  │
       │                  │
       ▼                  │
┌──────────────┐          │
│ json_builders│          │
│ + web_server │          │
│ (HTTP/WS UI) │          │
└──────┬───────┘          │
       │                  │
       ▼                  │
┌──────────────┐          │
│ web_routes_* │          │
│ + websocket  │          │
│ (API REST &  │          │
│ diffusion WS)│          │
└──────────────┘          │
       │                  │
       ▼                  │
┌──────────────┐          │
│  logger &    │◄─────────┘
│ watchdog_mgr │
│ (surveillance│
│  & journaux) │
└──────────────┘
       │
       ▼
┌──────────────┐
│ MQTT Bridge  │
│ (optionnel)  │
└──────────────┘
```

Ce schéma synthétise les flux majeurs : initialisation → acquisition → normalisation → diffusion multi-canaux, avec la configuration, la journalisation et le watchdog en soutien transversal.

## Modules
- **Initialisation système** – Création des mutex, montage SPIFFS, Event Bus, WiFi, lancement des tâches FreeRTOS (bridge, web, watchdog). Voir `README_system_init.md`.
- **Gestion de configuration** – Lecture/écriture JSON SPIFFS, notifications Event Bus, exposée via API REST. Voir `README_config_manager.md`.
- **Event Bus** – Publish/subscribe thread-safe, cache par type, statistiques et messages de statut. Voir `README_event_bus.md`.
- **Acquisition UART TinyBMS** – Modbus RTU, publication des données live. Voir `README_uart.md`.
- **Algorithme CVL** – Calcul dynamique des limites CVL/CCL/DCL. Voir `README_cvl.md`.
- **Watchdog Manager** – Supervision Task WDT, statistiques feed & exposition JSON. Voir `README_watchdog.md`.
- **Logger** – Journalisation SPIFFS + Serial avec rotation, niveau configurable. Voir `README_logger.md`.
- **Cartographie modules** – Résumé des responsabilités fichiers dans `README_MAPPING.md`.
- **Rapport de revue** – Statuts, tests et actions correctives par module dans `docs/REVUE_MODULES.md`.
- **Diagnostics avancés** – Interprétation des compteurs Watchdog/CAN/UART et procédures SPIFFS dans `docs/diagnostics_avances.md`.

## Installation
1. Installer [PlatformIO Core](https://platformio.org/) ou l'extension VS Code correspondante.
2. Cloner le dépôt et ouvrir le dossier `TinyBMS/`.
3. Installer les dépendances PlatformIO (automatique lors de la première compilation) :
   - `ArduinoJson@^6.21.0`
   - `ESPAsyncWebServer@^1.2.3`
   - `AsyncTCP@^1.1.1`
   - `CAN@^0.3.1`

## Utilisation
1. Personnaliser `data/config.json` (WiFi, TinyBMS, Victron, CVL, logging).
2. Compiler et flasher :
   ```bash
   pio run
   pio run --target upload
   pio run --target uploadfs
   ```
3. Alimenter l'ESP32 et connecter TinyBMS (UART GPIO16/17) et Victron (CAN GPIO4/5).
4. Accéder à l'interface : `http://tinybms-bridge.local` (ou IP). WebSocket disponible sur `/ws`.
5. API REST disponible :
   - `GET /api/status` : statut complet (compteurs, keepalive, alarmes, watchdog)
   - `GET /api/settings` / `POST /api/settings` : consultation/mise à jour de la configuration JSON
   - `GET /api/logs` / `DELETE /api/logs` : récupération et purge des journaux SPIFFS

## Tests
- Tests d'intégration hors matériel :
  ```bash
  python -m pytest tests/integration/test_end_to_end_flow.py
  ```
- Tests natifs CVL :
  ```bash
  g++ -std=c++17 -Iinclude tests/test_cvl_logic.cpp src/cvl_logic.cpp -o /tmp/test_cvl
  /tmp/test_cvl
  ```
- Le snapshot `tests/fixtures/status_snapshot.json` est vérifié pour garantir la présence des compteurs CAN/UART/Event Bus et des métadonnées d'alarmes.
- Les résultats et scénarios de tests à blanc sont centralisés dans `docs/REVUE_MODULES.md`.

## Dépannage CI
- Lors d'un `git commit` ou d'une ouverture de Pull Request, GitHub déclenche automatiquement les pipelines listés dans l'onglet **Checks**.
- Il est normal de voir un message "CI / build" ou "Native Tests" marqué comme `Started now` : cela signifie simplement que les jobs de compilation et de tests distants sont en cours d'exécution.
- Tant que ces jobs sont `in progress`, aucun changement local (notamment dans les fichiers `.cpp`) n'est requis. Il suffit d'attendre leur terminaison : ils passeront à l'état ✅ `Success` ou ❌ `Failure`, ce dernier cas fournissant un journal détaillant la cause à corriger.

## Plan d'actions prioritaire
1. **Industrialiser la chaîne de build** : automatiser la compilation ESP32 (PlatformIO) et l'exécution des tests natifs dans la CI.
2. **Étendre la couverture hors matériel** : ajouter des tests unitaires pour l'Event Bus (cache/stats) et un stub TinyBMS pour la couche UART.
3. **Documenter les diagnostics avancés** : guide terrain pour interpréter compteurs watchdog/CAN/UART et extractions de logs SPIFFS.

## Notes de développement
- Un **mode simulation** est disponible via `CONFIG_ENABLE_SIMULATION` dans `platformio.ini` pour rejouer des trames TinyBMS depuis `tests/fixtures/uart_samples.log` sans matériel.
- Les temps critiques (UART/CAN) sont analysables via `scripts/trace_tasks.py` qui exploite les compteurs FreeRTOS exportés en JSON.
- Les PR doivent inclure la mise à jour des **snapshots JSON** si des champs CAN ou Web changent, afin d'éviter des échecs de tests d'intégration.
