# Module Watchdog Manager

## Rôle
Encapsuler le watchdog matériel via `hal::IHalWatchdog`, suivre les intervalles de nourrissage et fournir une tâche de supervision. Les statistiques sont exposées à l'API (`/api/status`, `/api/watchdog`) et alimentent les diagnostics.

## API & comportement
- `WatchdogManager::begin(timeout_ms)` valide le timeout (`WATCHDOG_MIN/MAX_TIMEOUT`), configure le watchdog via `HalManager::watchdog()`, réinitialise les compteurs (`feed_count`, `min/max/avg interval`) et active la surveillance.
- `feed()` / `forceFeed()` déclenchent `hal::IHalWatchdog::feed()`, mettent à jour les stats et journalisent les feeds tardifs (>90 % du timeout). `feed()` ignore les appels trop rapprochés (`WATCHDOG_MIN_FEED_INTERVAL`).
- `enable()` / `disable()` délèguent directement au HAL (utilisés par l'API `/api/watchdog`).
- `checkHealth()` signale si le temps écoulé depuis le dernier feed dépasse le timeout.
- `printStats()` trace périodiquement les compteurs (utilisé par la tâche).
- `watchdogTask()` (FreeRTOS) boucle toutes les 10 s : log santé, imprime les stats, nourrit le watchdog sous `feedMutex` et surveille sa propre marge de stack.

## Statistiques exposées
- `time_since_last_feed_ms`, `time_until_timeout_ms`, `feed_count`, `min_feed_interval_ms`, `max_feed_interval_ms`, `average_feed_interval_ms`.
- `enabled`, `timeout_ms`, `health_ok`, `last_reset_reason` (dérivé de `esp_reset_reason`).
- Disponibles via `json_builders.cpp` dans la section `watchdog` et modifiables via `/api/watchdog` (`timeout_ms`, `enabled`).

## Synchronisation
- `feedMutex` protège les feeds lorsque plusieurs tâches (UART, CAN, CVL, WebSocket) nourrissent le watchdog.
- Les accès au HAL watchdog sont sérialisés dans `WatchdogManager` ; aucune autre partie du code n'interagit directement avec `esp_task_wdt`.

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` vérifie l'exposition des stats et l'intégration API.
- Tests manuels :
  - Désactiver temporairement les feeds (ex. suspendre `uartTask`) pour vérifier la détection `health_ok = false` et les logs.
  - Modifier le timeout via `/api/watchdog` et confirmer la prise en compte immédiate.
  - Observer la marge de stack (`watchdogTask stack`) pour dimensionner correctement la pile.
