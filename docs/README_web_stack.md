# Module Pile Web / API / JSON / WebSocket

## Rôle
Servir l'interface HTTP/WS du bridge TinyBMS↔Victron :
- Héberger les fichiers statiques (SPIFFS) et gérer le WebSocket `/ws` (`web_server_setup.cpp`, `websocket_handlers.cpp`).
- Exposer les API REST pour la configuration, la supervision et les diagnostics (`web_routes_api.cpp`, `web_routes_tinybms.cpp`).
- Construire les payloads JSON consolidés utilisés par l'API et les clients Web (`json_builders.cpp`).

## Fichiers couverts
- `src/web_server_setup.cpp`
- `src/web_routes_api.cpp`
- `src/web_routes_tinybms.cpp`
- `src/websocket_handlers.cpp`
- `src/json_builders.cpp`
- `include/web_routes.h`
- `include/websocket_handlers.h`
- `include/json_builders.h`

## Architecture
- `setupWebServer()` configure `AsyncWebServer` : handler WebSocket `/ws`, fichiers statiques SPIFFS (si `config.advanced.enable_spiffs`), routes API et options CORS (`config.web_server.enable_cors`). Une tâche FreeRTOS dédiée (`webServerTask`) appelle `ws.cleanupClients()` et surveille la marge de stack.
- `websocketTask` applique `optimization::WebsocketThrottle` (paramètres `web_server.websocket_*`) pour limiter le débit. Toutes les `interval_ms`, il récupère le dernier `LiveDataUpdate` via `eventBus.getLatest`, construit un JSON léger (`buildStatusJSON`) et diffuse vers tous les clients (`ws.textAll`).
- `json_builders` fournit les helpers REST :
  - `getStatusJSON()` (`StaticJsonDocument<2048>`) : live data, snapshots registres, stats UART/CAN/WebSocket/Event Bus/Watchdog/MQTT, messages de statut et alarmes.
  - `getConfigJSON()` (`StaticJsonDocument<640>`) : configuration TinyBMS pour l'éditeur.
  - `getSystemConfigJSON()` (`StaticJsonDocument<3072>`) : configuration globale (WiFi, hardware, tinybms, cvl, victron, mqtt, web, logging, advanced, watchdog) + état réseau.

## Synthèse des routes principales
| Endpoint | Méthode | Description | Source |
| --- | --- | --- | --- |
| `/api/status` | GET | Snapshot complet (live data, stats, watchdog, alarmes, MQTT). | `setupAPIRoutes` → `getStatusJSON()` |
| `/api/config/system` | GET/PUT | Lecture & mise à jour complète (`applySettingsPayload`, `config.save()`). | `web_routes_api.cpp` |
| `/api/config` | GET | Snapshot structuré (wifi, hardware, cvl, victron, logging, advanced, mqtt). | `buildSettingsSnapshot()` |
| `/api/config/wifi`, `/hardware`, `/cvl`, `/victron`, `/logging`, `/mqtt` | POST | Mise à jour ciblée avec persistance optionnelle. | `web_routes_api.cpp` |
| `/api/config/save` | POST | Sauvegarde `config.save()` (après modifications en mémoire). | `web_routes_api.cpp` |
| `/api/config/import` | POST | Import JSON complet (mêmes règles que PUT `/api/config/system`). | `web_routes_api.cpp` |
| `/api/config/reload` | POST | Rechargement `/config.json` via `ConfigManager::begin()`. | `web_routes_api.cpp` |
| `/api/config/reset` / `/api/system/factory-reset` | POST | Suppression config/logs + reboot optionnel. | `web_routes_api.cpp` |
| `/api/system` | GET | Santé WiFi/SPIFFS/heap. | `web_routes_api.cpp` |
| `/api/system/restart` | POST | Demande de redémarrage (watchdog, feed protégé). | `web_routes_api.cpp` |
| `/api/memory` | GET | Informations heap/PSRAM. | `web_routes_api.cpp` |
| `/api/can/mapping` | GET | Mapping PGN (`victron_can_mapping`). | `buildVictronCanMappingDocument()` |
| `/api/logs/download`, `/api/logs/clear`, `/api/logs/level` | GET/POST | Gestion fichier logs via `Logger`. | `web_routes_api.cpp` |
| `/api/watchdog` | GET/PUT | Consultation & configuration watchdog. | `web_routes_api.cpp` |
| `/api/stats/reset`, `/api/statistics` | POST/GET | Reset stats EventBus + squelette d'export. | `web_routes_api.cpp` |
| `/api/hardware/test/uart` / `/api/hardware/test/can` | GET | Tests de communication TinyBMS/CAN. | `web_routes_api.cpp` |
| `/api/tinybms/registers*` | GET/POST | Lecture/écriture registres via `TinyBMSConfigEditor`. | `web_routes_tinybms.cpp` |

## Synchronisation & sécurité
- `configMutex` protège toutes les lectures/écritures `ConfigManager` (`applySettingsPayload`, exposition JSON, throttle WebSocket).
- `feedMutex` est utilisé avant les opérations critiques (reboot, watchdog) pour éviter les feeds concurrents.
- `statsMutex` protège l'accès aux compteurs `BridgeStats` lors de la construction JSON.
- Les payloads sont sérialisés avec `ArduinoJson` (tailles documentées ci-dessus) pour maîtriser l'utilisation RAM.

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` couvre `/api/status`, `/api/config/system`, WebSocket et mapping PGN.
- Tests manuels :
  - Activer `config.web_server.enable_cors` et vérifier l'entête `Access-Control-Allow-Origin`.
  - Utiliser `/api/tinybms/registers/read-all` pour valider la taille du JSON et la gestion throttle.
  - Tester `/api/config/import` avec un JSON complet et s'assurer que les valeurs sont appliquées puis persistées.
  - Vérifier `/api/logs/level` en changeant la verbosité et en observant la sortie Serial.
