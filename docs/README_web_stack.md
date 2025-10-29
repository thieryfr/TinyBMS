# Module Pile Web / API / JSON / WebSocket

## Rôle
Fournir l'interface HTTP/WS du TinyBMS bridge :
- Servir l'UI statique depuis SPIFFS et gérer les connexions WebSocket (`web_server_setup.cpp`, `websocket_handlers.cpp`).
- Exposer les API REST pour le pilotage système, la configuration et la maintenance (`web_routes_api.cpp`, `web_routes_tinybms.cpp`).
- Construire les payloads JSON consolidés (status, configuration, diagnostics) consommés par l'API et les clients Web (`json_builders.cpp`).

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
- `setupWebServer()` configure `AsyncWebServer` : handler WebSocket `/ws`, fichiers statiques SPIFFS, routes API, option CORS selon `config.web_server.enable_cors`. La tâche FreeRTOS `webServerTask` exécute `ws.cleanupClients()` toutes les secondes et trace la marge de stack.
- `websocketTask` (lancée depuis `system_init`) publie périodiquement le statut live. Elle :
  - Charge la configuration throttle (`web_server.websocket_min_interval_ms`, `websocket_burst_window_ms`, `websocket_burst_max`, `websocket_max_payload_bytes`).
  - Utilise `optimization::WebsocketThrottle` pour limiter le nombre de frames.
  - Récupère le dernier `LiveDataUpdate` via `eventBus.getLatest`, construit un JSON compact (`buildStatusJSON`) et envoie `ws.textAll`.
- `json_builders` fournit les helpers appelés depuis les routes :
  - `getStatusJSON()` : live data, snapshots de registres, statistiques bridge/event bus, watchdog, alarmes.
  - `getConfigJSON()` : configuration TinyBMS lue dans `TinyBMS_Config` (UART).
  - `getSystemConfigJSON()` : configuration globale (WiFi, hardware, CVL, Victron, web, logging, watchdog) + état réseau courant.

## Synthèse des routes principales
| Endpoint | Méthode | Description | Source |
| --- | --- | --- | --- |
| `/api/status` | GET | Payload complet (live data, stats, watchdog, alarmes). | `setupAPIRoutes` → `getStatusJSON()` |
| `/api/config/system` | GET/PUT | Dump + mise à jour de l'ensemble des paramètres (`applySettingsPayload`, `config.save()`). | `web_routes_api.cpp` |
| `/api/config` | GET | Snapshot structuré (wifi, hardware, CVL, Victron, logging, system). | `buildSettingsSnapshot()` |
| `/api/config/wifi`, `/hardware`, `/cvl`, `/victron`, `/logging` | POST | Mise à jour ciblée (réutilise `applySettingsPayload`). | `web_routes_api.cpp` |
| `/api/config/save` | POST | Applique `settings` et persiste `config.json`. | `web_routes_api.cpp` |
| `/api/config/import` | POST | Importe un JSON complet (mêmes règles que `PUT /api/config/system`). | `web_routes_api.cpp` |
| `/api/config/reload` | POST | Relit `/config.json` via `ConfigManager::begin()`. | `web_routes_api.cpp` |
| `/api/config/reset` / `/api/system/factory-reset` | POST | Supprime `config.json` et/ou `logs.txt`, reboot optionnel. | `web_routes_api.cpp` |
| `/api/system` | GET | Santé WiFi/SPIFFS/heap. | `web_routes_api.cpp` |
| `/api/system/restart` / `/api/reboot` | POST | Demande redémarrage (avec feed watchdog). | `web_routes_api.cpp` |
| `/api/memory` | GET | Informations tas + PSRAM. | `web_routes_api.cpp` |
| `/api/can/mapping` | GET | Expose le mapping PGN (définitions `victron_can_mapping`). | `buildVictronCanMappingDocument()` |
| `/api/logs/clear` / `/api/logs/download` | POST/GET | Gestion du fichier `logs.txt`. | `web_routes_api.cpp` |
| `/api/watchdog` | GET/PUT | Consultation & modification du watchdog (timeout, enable/disable). | `web_routes_api.cpp` |
| `/api/stats/reset`, `/api/statistics` | POST/GET | Reset stats EventBus + export squelette (placeholder historique). | `web_routes_api.cpp` |
| `/api/hardware/test/uart` | GET | Transaction Modbus de test `readTinyRegisters`. | `web_routes_api.cpp` |
| `/api/hardware/test/can` | GET | Dump stats driver CAN. | `web_routes_api.cpp` |
| `/api/tinybms/registers` | GET | Liste les registres RW via `TinyBMSConfigEditor`. | `web_routes_tinybms.cpp` |
| `/api/tinybms/register` | GET/POST | Lecture/écriture unitaire (addr ou `key`). | `web_routes_tinybms.cpp` |
| `/api/tinybms/registers/read-all` | POST | Lecture batch de tous les registres RW. | `web_routes_tinybms.cpp` |
| `/api/tinybms/registers/batch` | POST | Lecture multiple sur liste d'adresses. | `web_routes_tinybms.cpp` |

## Construction JSON (principaux champs)
- **Live data (`getStatusJSON`)** : `live_data` contient voltage/courant/SOC/SOH/température, min/max cellules, bits balancing, `registers[]` (address, raw, word_count, value, metadata, binding fallback). Les stats contiennent sections `uart`, `can`, `websocket`, `keepalive`, `event_bus`, `mqtt` (via `VictronMqttBridge::appendStatus`).
- **Alarmes** : `alarms[]` rassemble le dernier `AlarmRaised`, `AlarmCleared`, `WarningRaised` en indiquant code, sévérité, message, timestamp. `alarms_active` reflète l'état courant.
- **Watchdog** : `watchdog` expose `enabled`, `timeout_ms`, `time_since_last_feed_ms`, `feed_count`, `health_ok`, `last_reset_reason`, `time_until_timeout_ms`.
- **System config JSON** : inclut WiFi courant (`mode_active`, `ip`, `rssi`), hardware UART/CAN, `tinybms` (tous les paramètres poll adaptatifs), `cvl_algorithm`, `victron` (avec `thresholds`), `web_server`, `logging`, `advanced`, `mqtt`, `watchdog_config`.

## Synchronisation & sécurité
- Tous les accès lecture/écriture `ConfigManager` se font sous `configMutex` (timeouts 100 ms). `applySettingsPayload` centralise la logique de mise à jour et appelle `config.save()` si `persist=true`.
- Les routes critiques (`/api/watchdog`, `/api/system/restart`) utilisent `feedMutex` pour synchroniser avec `WatchdogManager` avant de modifier l'état.
- `buildStatusJSON` et `websocketTask` lisent `bridge.stats` sous `statsMutex` pour éviter les races.
- Les réponses JSON sont serialisées via `ArduinoJson` sur buffer statique/dynamique calibré (2 Ko pour status, 3 Ko pour config système, 16 Ko pour listing de registres).

## Tests
- Intégration Python : `python -m pytest tests/integration/test_end_to_end_flow.py` vérifie `/api/status`, `/api/settings` (config snapshot), notifications WebSocket et mapping PGN.
- Tests manuels recommandés :
  - Activer `config.web_server.enable_cors` et vérifier l'entête `Access-Control-Allow-Origin`.
  - Simuler une perte de watchdog (désactivation via `/api/watchdog`) et s'assurer que `watchdog` dans `/api/status` se met à jour.
  - Utiliser `/api/tinybms/registers/read-all` pour confirmer la taille du JSON (vérifier logs `[API] POST ...`).
  - Exporter/importer la configuration via `/api/config/system` et valider que les modifications apparaissent dans `/api/config`.

## Améliorations possibles
- Ajouter de l'authentification HTTP (`enable_auth`) et vérifier l'intégration avec les routes critiques (actuellement placeholders).
- Étendre `/api/statistics` pour retourner des données consolidées (actuellement squelette vide).
- Mettre en place des tests unitaires natifs pour `applySettingsPayload` (valider la conversion des anciens noms de champs vers les nouveaux).
- Ajouter une compression optionnelle (gzip) sur `/api/status` lorsque la taille dépasse `websocket_max_payload_bytes`.
