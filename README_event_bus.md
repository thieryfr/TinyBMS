# Module Event Bus

## Rôle
Fournir un bus de messages centralisé (publish/subscribe) pour découpler les tâches FreeRTOS. Gère le cache des derniers événements, les statistiques de file et la distribution asynchrone via une tâche dédiée.

## API essentielle
- `EventBus::begin(size_t queue_size)` : crée la queue FreeRTOS, le mutex et la tâche de dispatch.
- `publish*` : wrappers spécialisés (live data, alarmes, statut, changements de configuration, CVL).
- `subscribe()` / `unsubscribe()` : gestion des abonnés par type d'événement.
- `getLatest()` / `getLatestLiveData()` : accès cache pour WebSocket/JSON.
- `getStats()` / `resetStats()` : statistiques pour diagnostics.

## Utilisation type
1. Initialiser le bus dans `setup()` via `eventBus.begin()`.
2. Publier depuis les tâches (UART, CAN, CVL, Watchdog, Web) en fournissant un `source_id`.
3. Consommer dans `websocketTask` ou `json_builders` via `getLatest*` pour éviter de verrouiller des queues dédiées.

## Concurrence
- Mutex interne (`bus_mutex_`) protège les abonnés et le cache.
- Les callbacks sont exécutés hors section critique pour éviter les blocages.

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` valide la cohérence des publications/consommations enregistrées.
- Prévoir des tests natifs pour vérifier `publish` + `getLatest` (mock FreeRTOS nécessaire).

## Bonnes pratiques
- Limiter la taille des structures d'événement pour rester sous `sizeof(BusEvent::data)`.
- Utiliser `EVENT_BUS_MAX_SUBSCRIBERS_PER_TYPE` pour éviter les saturations.
