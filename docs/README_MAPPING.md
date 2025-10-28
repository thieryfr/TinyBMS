# Bridge Module Mapping

| Path | Description |
| --- | --- |
| `src/bridge_core.cpp` | Initialise le bridge TinyBMS↔Victron, capture les intervalles depuis la configuration et crée les tâches UART/CAN/CVL. |
| `src/bridge_uart.cpp` | Interroge TinyBMS, publie les mesures sur l'Event Bus, maintient les statistiques UART et déclenche les alarmes communication. |
| `src/bridge_can.cpp` | Construit et transmet les PGN Victron, publie les alarmes 0x35A, gère la télémétrie CAN et les compteurs de driver. |
| `src/bridge_cvl.cpp` | Orchestration de l'algorithme CVL : snapshots config, calculs `computeCvlLimits`, publication Event Bus + stats bridge. |
| `src/bridge_keepalive.cpp` | Surveille les keep-alive Victron, déclenche les alarmes de perte et met à jour `bridge.stats`. |
| `include/tinybms_victron_bridge.h` | Déclare la façade bridge, les structures partagées (stats, snapshots, timers) et les signatures des tâches FreeRTOS. |
| `include/config_manager.h` / `src/config_manager.cpp` | Charge/sauvegarde la configuration SPIFFS (WiFi, TinyBMS, Victron, CVL, logging, web, avancé) avec notifications Event Bus. |
| `include/watchdog_manager.h` / `src/watchdog_manager.cpp` | Abstraction Task WDT : stats, API JSON, tâche dédiée `watchdogTask` et helpers thread-safe. |
| `include/event_bus.h` / `src/event_bus.cpp` | Bus d'événements singleton avec cache par type, statistiques, publication de messages statut/alarme/config. |
| `src/json_builders.cpp` | Génère les payloads JSON `/api/status` et `/api/config`, agrège les compteurs UART/CAN/EventBus/Watchdog. |
| `src/web_server_setup.cpp` | Configure AsyncWebServer/WebSocket, applique les réglages `config.web_server` et crée la tâche FreeRTOS correspondante. |
| `src/web_routes_api.cpp` | Routes REST (status, settings, logs, diagnostics) avec protections mutex et publication Event Bus. |
| `src/web_routes_tinybms.cpp` | Endpoints spécialisés pour l'éditeur TinyBMS (lecture/écriture registres via `TinyBMSConfigEditor`). |
| `src/websocket_handlers.cpp` | Gestion des clients WS, diffusion périodique des snapshots Event Bus et sérialisation allégée. |
| `src/tinybms_config_editor.cpp` | Fournit un catalogue de registres TinyBMS, lecture/écriture via UART et intégration web. |
| `include/can_driver.h` / `src/can_driver.cpp` | Abstraction driver CAN (simulation) pour décorréler la logique bridge du matériel. |
