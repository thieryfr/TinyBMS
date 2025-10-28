# Module ConfigManager

## Rôle
Charger, valider et sauvegarder la configuration JSON stockée sur SPIFFS. Propager les changements aux autres composants via l'Event Bus.

## Fonctions principales
- `ConfigManager::begin()` : Monte SPIFFS, charge le fichier, remplit les structures (`wifi`, `hardware`, `tinybms`, `victron`, `cvl`, `web_server`, `logging`, `advanced`) puis publie un événement `EVENT_CONFIG_CHANGED`.
- `ConfigManager::save()` : Sérialise les structures actuelles vers le fichier JSON et republie un événement de changement.
- `load*` / `save*` : Sous-fonctions spécialisées par domaine (WiFi, matériel, CVL...).

## Verrous et threads
- Tous les accès aux structures publiques doivent être protégés par `configMutex`.
- Les opérations SPIFFS sont encapsulées dans `begin()` / `save()` ; éviter les appels concurrents à `SPIFFS.begin()` depuis d'autres modules.

## Consommateurs
- Bridge UART/CAN (timings, pins, thresholds),
- Web/API/JSON (exposition de la configuration),
- Logger (niveau de log et outputs),
- Watchdog (timeout),
- Event Bus (pour diffuser les changements).

## Tests recommandés
- Charger `data/config.json` et vérifier la bonne propagation via `python -m pytest tests/integration/test_end_to_end_flow.py`.
- Ajouter des tests unitaires simulant un JSON incomplet (à implémenter via mocks ArduinoJson).

## Améliorations suggérées
- Mutualiser le montage SPIFFS avec le Logger pour éviter les montages répétés.
- Étendre le schéma JSON de test pour inclure de nouveaux champs avant déploiement.
