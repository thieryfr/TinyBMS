# Module Event Bus

## Rôle
Fournir un bus de messages centralisé (publish/subscribe) pour découpler les tâches FreeRTOS. Gère le cache des derniers événements par type, les statistiques de file, les notifications de statut et la distribution asynchrone via une tâche dédiée (`eventBusDispatch`).

## API essentielle
- `EventBus::begin(size_t queue_size)` : crée la queue FreeRTOS, le mutex, la tâche `eventBusDispatch` et initialise les stats.
- `publish*` : wrappers spécialisés (live data, alarmes, avertissements, statut système, changements de configuration, CVL, keepalive).
- `subscribe()` / `unsubscribe()` : gestion des abonnés par type d'événement (callbacks exécutés hors section critique).
- `getLatest()` / `getLatestLiveData()` / `getLatest(EVENT_TYPE, BusEvent&)` : accès cache pour WebSocket/JSON.
- `getStats()` / `resetStats()` : statistiques (événements publiés/distribués, overruns, profondeur queue, abonnés).

## Utilisation type
1. Initialiser le bus tôt dans `initializeSystem()` via `eventBus.begin(EVENT_BUS_QUEUE_SIZE)`.
2. Publier depuis les tâches (UART, CAN, CVL, Watchdog, Web) en fournissant un `source_id` (voir `event_types.h`).
3. Consommer dans `websocketTask`, `json_builders` ou l'éditeur TinyBMS via `getLatest*` pour éviter des queues supplémentaires.

## Concurrence
- Mutex interne (`bus_mutex_`) protège les abonnés, le cache et les statistiques.
- Les callbacks sont exécutés hors section critique pour éviter les blocages.
- La queue FreeRTOS est unique (`event_queue_`), dimensionnée via `EVENT_BUS_QUEUE_SIZE` (configurable dans `event_bus_config.h`).

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` valide la cohérence des publications/consommations et la présence des compteurs dans `status_snapshot.json`.
- Tests natifs à prévoir : simulation FreeRTOS (ou wrapper) pour valider `publish` + `getLatest` + `getStats`.

## Bonnes pratiques
- Limiter la taille des structures d'événement pour rester sous `sizeof(BusEvent::data)` (voir `event_types.h`).
- Utiliser `EVENT_BUS_MAX_SUBSCRIBERS_PER_TYPE` pour éviter les saturations.
- Activer `EVENT_BUS_LOG_PUBLICATIONS` et `EVENT_BUS_STATS_ENABLED` uniquement pour le debug (impact performance).
- Toujours vérifier `eventBus.isInitialized()` avant publication pendant la phase de boot.
- Les nouveaux modules d'I/O (MQTT, Modbus RTU, etc.) doivent impérativement publier et consommer via ce bus interne afin de
  préserver l'architecture découplée existante et éviter les accès directs concurrents aux registres TinyBMS.
