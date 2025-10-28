# Module Initialisation Système

## Rôle
Coordonner la mise sous tension de l'ESP32 : création des mutex globaux, chargement de la configuration, initialisation du logger et du watchdog, montage SPIFFS, démarrage de l'Event Bus puis lancement des tâches FreeRTOS (bridge UART/CAN/CVL, serveur web, WebSocket, watchdog). Les fonctions principales résident dans `setup()`/`loop()` et `initializeSystem()`.

## Flux de données
1. `setup()` crée les mutex (`uartMutex`, `feedMutex`, `configMutex`) puis charge la configuration via `ConfigManager::begin()`.
2. Le logger et le watchdog sont initialisés en utilisant les paramètres de configuration (niveau de log, timeout WDT, débit Serial).
3. `initializeSystem()` enchaîne : montage SPIFFS, démarrage de l'Event Bus (`eventBus.begin()`), initialisation WiFi (client + fallback AP), démarrage du bridge (`bridge.begin()`), création des tâches bridge (`Bridge_CreateTasks`), initialisation de l'éditeur TinyBMS, création des tâches Web (`initWebServerTask`, `websocketTask`) et du watchdog (`WatchdogManager::watchdogTask`).
4. Chaque étape publie un message `EVENT_STATUS_MESSAGE` sur l'Event Bus pour la supervision WebSocket/JSON.

## Dépendances principales
- ConfigManager pour l'accès aux paramètres WiFi, bridge, web et logging (protégé par `configMutex`).
- Logger pour tracer les étapes critiques (SPIFFS, WiFi, tâches, erreurs).
- WatchdogManager pour protéger les boucles d'initialisation (`feedMutex`).
- EventBus pour publier les messages d'état et alimenter l'interface web.
- AsyncWebServer / WebSocket via `web_server_setup.cpp` pour l'hébergement HTTP.

## Paramètres clés
- `config.wifi.*` et `config.hardware.*` pour la connectivité.
- Intervalles d'interrogation bridge/CAN/CVL (`config.tinybms`, `config.victron`).
- Paramètres serveur web (`config.web_server` : port, CORS, WebSocket).
- Timeout watchdog (`config.advanced.watchdog_timeout_s`).
- Niveau et options de journalisation (`config.logging.*`).

## Tests recommandés
- `python -m pytest tests/integration/test_end_to_end_flow.py` pour vérifier la séquence de démarrage logique et la présence des statuts dans le snapshot.
- `g++ ... tests/test_cvl_logic.cpp` pour valider que la tâche CVL reste cohérente avec le moteur natif.
- Vérification manuelle des logs au démarrage (SPIFFS + Serial) et du statut `/api/status` (sections `status_message`, `watchdog`).

## Points de vigilance
- Vérifier la disponibilité des mutex avant tout accès aux ressources partagées (`configMutex`, `feedMutex`, `uartMutex`).
- Documenter chaque nouvelle tâche FreeRTOS et l'ajouter dans `initializeSystem()` / `Bridge_CreateTasks()`.
- Publier un `STATUS_LEVEL_ERROR` sur l'Event Bus en cas d'échec d'initialisation pour visibilité UI.
