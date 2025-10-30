# Module ConfigManager

## Rôle
Charger, valider et sauvegarder la configuration JSON (`/config.json`) via la couche de stockage du HAL. Les structures typées (`wifi`, `hardware`, `tinybms`, `victron`, `cvl`, `mqtt`, `web_server`, `logging`, `advanced`) sont exposées à l'ensemble du firmware sous protection `configMutex`. Chaque modification est signalée sur l'Event Bus (`ConfigChanged`).

## Fonctionnement
1. `ConfigManager::begin(path)` ouvre le fichier via `hal::HalManager::storage()`, désérialise le JSON (`DynamicJsonDocument 6144 bytes`), remplit les blocs de configuration (`load*`) puis publie un événement `ConfigChanged` générique (`config_path="*"`). Si le fichier est absent ou invalide, des valeurs par défaut sont conservées et le chargement échoue proprement.
2. `ConfigManager::save()` sérialise l'état courant (`save*`), réécrit le fichier (mode `Write`) et republie `ConfigChanged`.
3. Les helpers `loadLoggingConfig`, `loadMqttConfig`, etc. assurent la rétro-compatibilité en acceptant d'anciens noms de champs quand ils existent encore dans le JSON.
4. `printConfig()` trace un résumé complet via le `Logger` pour diagnostic au boot.

## Synchronisation
- Tous les accès à `config.*` sont protégés par `configMutex` (timeouts 100 ms). Les modules bridge, Web, watchdog, MQTT et logger respectent ce verrou.
- Le stockage HAL est partagé avec le Logger (`/logs.txt`) : pas de montage SPIFFS direct dans `ConfigManager`.
- L'événement `ConfigChanged` inclut chemin / anciennes / nouvelles valeurs pour consommation côté MQTT ou UI.

## Consommateurs majeurs
- **Bridge TinyBMS↔Victron** : timings UART/CAN/CVL, seuils Victron, noms fabricant/batterie.
- **Algorithme CVL** : hystérésis SOC, protections cellules, mode sustain, limites CCL/DCL.
- **Pile Web** : exposition et modification (`applySettingsPayload`, `/api/config/*`, `/api/watchdog`).
- **Logger & Watchdog** : niveau de log, timeout watchdog, choix backend stockage.
- **Victron MQTT Bridge** : URI, identifiants, QoS, root topic, TLS.

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` vérifie l'import de `data/config.json`, la restitution `/api/status` et la sauvegarde via `/api/config/system`.
- Tests manuels :
  - Supprimer `/config.json` puis redémarrer pour valider les valeurs par défaut et l'événement `ConfigChanged`.
  - Modifier dynamiquement des champs via l'API (`/api/config/logging`, `/api/config/victron`) et contrôler la persistance (`config.save()`).

## Améliorations possibles
- Ajouter des tests unitaires natifs ciblant `load*/save*` avec des JSON minimaux pour garantir la compatibilité ascendante.
- Exposer un résumé de validation dans `/api/status` (ex. `config.loaded`, champs manquants) afin d'aider au diagnostic UI.
- Factoriser le buffer JSON entre `begin()` et `save()` pour réduire l'utilisation mémoire dans les environnements contraints.
