# Module ConfigManager

## Rôle
Charger, valider et sauvegarder la configuration JSON stockée sur SPIFFS. Fournir un accès thread-safe aux paramètres (WiFi, TinyBMS, Victron, CVL, web, logging, options avancées) et propager les changements aux autres composants via l'Event Bus.

## Fonctions principales
- `ConfigManager::begin()` : monte SPIFFS (avec formatage en secours), charge `/config.json`, renseigne les structures (`wifi`, `hardware`, `tinybms`, `victron`, `cvl`, `web_server`, `logging`, `advanced`) puis publie `EVENT_CONFIG_CHANGED`.
- `ConfigManager::save()` : sérialise les structures courantes dans le JSON, réécrit le fichier et republie `EVENT_CONFIG_CHANGED`.
- `load*` / `save*` : sous-fonctions par domaine (WiFi, matériel, TinyBMS, Victron/thresholds, CVL, Web, logging, avancé).
- `printConfig()` : trace la configuration active dans les logs pour diagnostic.

## Verrous et threads
- Tous les accès aux structures publiques doivent être protégés par `configMutex` (les modules CAN, Web et CVL prennent le mutex avant lecture/écriture).
- `ConfigManager::begin()` et `save()` encapsulent les accès SPIFFS ; éviter de remonter `SPIFFS.begin()` ailleurs.
- L'Event Bus peut être indisponible au démarrage : vérifier `eventBus.isInitialized()` avant de publier depuis d'autres modules.

## Consommateurs
- Bridge UART/CAN/CVL (timings, pins, seuils, keepalive),
- Web/API/JSON (exposition + édition via `/api/settings`),
- Logger (niveau de log, options `log_can_traffic` / `log_uart_traffic` / `log_cvl_changes`, débit Serial),
- Watchdog (timeout),
- TinyBMS Config Editor (catalogue registres TinyBMS),
- Event Bus (publication `EVENT_CONFIG_CHANGED`).

## Tests recommandés
- Charger `data/config.json` et vérifier la bonne propagation via `python -m pytest tests/integration/test_end_to_end_flow.py` (assertions sur `/api/status` et `/api/settings`).
- Ajouter des tests unitaires simulant un JSON incomplet (à implémenter via mocks ArduinoJson) et vérifier la publication `EVENT_CONFIG_CHANGED`.

## Améliorations suggérées
- Mutualiser le montage SPIFFS avec le Logger pour éviter les montages répétés.
- Étendre le schéma JSON de test pour inclure les champs web/logging avancés avant déploiement.
- Ajouter un endpoint de validation offline pour détecter les oublis de champs obligatoires.
