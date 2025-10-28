# Module Initialisation Système

## Rôle
Coordonner la mise sous tension de l'ESP32 : création des mutex globaux, chargement de la configuration, démarrage du logger, initialisation du watchdog et lancement des tâches FreeRTOS (bridge, web, surveillance). Les fonctions principales résident dans `setup()`/`loop()` et `initializeSystem()`.

## Flux de données
1. `setup()` crée les mutex (`uartMutex`, `feedMutex`, `configMutex`) puis charge la configuration via `ConfigManager::begin()`.
2. Le logger et le watchdog sont initialisés en utilisant les paramètres de configuration.
3. `initializeSystem()` (dans `system_init.cpp`) enchaîne `initializeSPIFFS()`, `initializeWiFi()`, `initializeBridge()`, la création des tâches Bridge/UART/CAN/CVL et le démarrage du serveur web.
4. Les statuts sont publiés sur l'Event Bus pour consommation par le WebSocket/JSON.

## Dépendances principales
- ConfigManager pour l'accès aux paramètres WiFi et matériels.
- Logger pour tracer les étapes critiques.
- WatchdogManager pour protéger les boucles d'initialisation.
- EventBus pour publier les messages d'état.

## Paramètres clés
- `config.wifi.*` et `config.hardware.*` pour la connectivité.
- Intervalles d'interrogation du bridge (`config.tinybms`, `config.victron`).
- Timeout watchdog (`config.advanced.watchdog_timeout_s`).

## Tests recommandés
- `python -m pytest tests/integration/test_end_to_end_flow.py` pour vérifier la séquence de démarrage logique via fixtures.
- Vérification manuelle des logs au démarrage (SPIFFS + Serial).

## Points de vigilance
- Vérifier la disponibilité des mutex avant tout accès aux ressources partagées.
- Documenter chaque nouvelle tâche FreeRTOS et l'ajouter dans `initializeSystem()`.
