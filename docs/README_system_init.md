# Module Initialisation Système

## Rôle
Orchestrer le démarrage de la plateforme ESP32 : configuration du HAL (`HalManager`), création des mutex globaux, chargement de la configuration, initialisation du logger, du watchdog, du WiFi, du stockage (SPIFFS via HAL), du bridge TinyBMS↔Victron, du bridge MQTT et de la pile Web. Toutes les étapes publient des `StatusMessage` sur l'Event Bus pour la supervision UI.

## Étapes principales (`initializeSystem`)
1. **HAL & stockage** : `buildHalConfig()` applique les réglages UART/CAN/watchdog/SPIFFS issus de `config`. `initializeSPIFFS()` ne monte pas directement le FS mais vérifie la disponibilité SPIFFS via HAL et journalise le contenu quand activé (`config.advanced.enable_spiffs`).
2. **Mappings** : charge `tiny_read_mapping` et `victron_can_mapping` depuis SPIFFS pour alimenter UART/MQTT/CAN (`initializeTinyReadMapping`, `initializeVictronCanMapping`).
3. **Event Bus** : `eventBus.resetStats()` prépare les compteurs avant diffusion.
4. **WiFi** : `initializeWiFi()` configure STA/AP selon `config.wifi`, gère le fallback AP, et publie des statuts.
5. **MQTT** : `initializeMqttBridge()` active `VictronMqttBridge` si `config.mqtt.enabled`, applique `BrokerSettings` et crée la tâche FreeRTOS `mqttLoopTask` (reconnexion périodique).
6. **Bridge TinyBMS↔Victron** : `Bridge_BuildAndBegin()` prépare UART/CAN/CVL via `bridge.begin()` puis `Bridge_CreateTasks()` démarre `uartTask`, `canTask`, `cvlTask`.
7. **Config editor + Web** : `initializeConfigEditor()` prépare le catalogue TinyBMS. `initWebServerTask()` crée la tâche AsyncWebServer et `websocketTask` diffuse les snapshots JSON. `WatchdogManager::watchdogTask` surveille l'alimentation du watchdog.
8. **Statuts finaux** : chaque succès/échec publie un `StatusMessage` (`StatusLevel::Notice/Warning/Error`), permettant à `/api/status` de refléter la progression.

## Ressources partagées
- Mutex globaux : `configMutex`, `uartMutex`, `feedMutex`, `statsMutex` (créés dans `setup()` avant `initializeSystem`).
- Tâches FreeRTOS créées via `createTask` avec journalisation `[TASK]` et check `uxTaskGetStackHighWaterMark`.
- `WatchdogManager` configuré depuis `config.advanced.watchdog_timeout_s` et nourri dans les différentes tâches via `feedMutex`.

## Interactions externes
- `setup()` : instancie le HAL (`hal::createEsp32Factory`), initialise `logger.begin(config)`, `config.begin()`, puis appelle `initializeSystem()`.
- `loop()` : se contente d'endormir la boucle principale, toutes les actions étant pilotées par les tâches.

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` vérifie la séquence d'initialisation simulée, la publication des statuts, la création des tâches et la présence des mappings dans `/api/status`.
- Tests manuels :
  - Désactiver `config.advanced.enable_spiffs` pour vérifier le mode dégradé (statut « SPIFFS disabled »).
  - Forcer l'échec WiFi (SSID incorrect) et constater l'activation AP et les messages Warning.
  - Désactiver `config.mqtt.enabled` pour valider que la tâche MQTT est ignorée.
