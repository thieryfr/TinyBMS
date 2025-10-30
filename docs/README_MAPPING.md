# Bridge Module Mapping

| Path | Description |
| --- | --- |
| `src/system_init.cpp` | Démarre le HAL, charge la configuration, initialise WiFi/SPIFFS/MQTT/bridge, crée les tâches FreeRTOS et publie les `StatusMessage`. |
| `src/bridge_core.cpp` | Initialise le bridge TinyBMS↔Victron (HAL UART/CAN, AdaptivePolling, stats) et expose `Bridge_CreateTasks`. |
| `src/bridge_uart.cpp` | Effectue les lectures Modbus TinyBMS, met à jour `TinyBMS_LiveData`, publie `LiveDataUpdate`/`MqttRegisterValue` et gère les alarmes UART. |
| `src/bridge_can.cpp` / `src/bridge_keepalive.cpp` | Construit les PGN VE.Can, applique les mappings dynamiques, gère le keep-alive 0x305 et maintient les compteurs d'énergie/CAN. |
| `src/bridge_cvl.cpp` / `src/cvl_logic.cpp` | Calcule CVL/CCL/DCL à partir du SOC, des seuils configurés et des protections cellules, publie `CVLStateChanged`. |
| `src/config_manager.cpp` | Charge/sauvegarde `/config.json` via `hal::IHalStorage`, expose les structures `config.*` et publie `ConfigChanged`. |
| `src/logger.cpp` | Journalisation centralisée (Serial + stockage HAL) avec rotation et API `/api/logs`. |
| `src/watchdog_manager.cpp` | Abstraction du watchdog matériel : configuration, feeds synchronisés, stats et tâche `watchdogTask`. |
| `src/mqtt/victron_mqtt_bridge.cpp` | Relaye les événements TinyBMS/alarme vers MQTT, gère la reconnexion, expose l'état dans `/api/status`. |
| `src/json_builders.cpp` | Construit les payloads JSON (`/api/status`, `/api/config`, `/api/config/system`, mapping CAN) en agrégeant live data, stats et config. |
| `src/web_routes_api.cpp` / `src/web_routes_tinybms.cpp` | Implémentent les endpoints REST (status, config, diagnostics, watchdog, TinyBMS RW). |
| `src/websocket_handlers.cpp` / `src/web_server_setup.cpp` | Configurent `AsyncWebServer`, le WebSocket et la diffusion périodique des snapshots Event Bus. |
| `include/event/event_bus_v2.h` / `.tpp` | Bus d'événements templatisé thread-safe (publish/subscribe, cache par type, statistiques). |
| `include/tinybms_victron_bridge.h` | Déclare la façade bridge (`BridgeStats`, tâches, paramètres) partagée par les modules CAN/UART/CVL. |
