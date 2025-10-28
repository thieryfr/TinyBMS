# Module Watchdog Manager

## Rôle
Configurer et piloter le watchdog matériel de l'ESP32 (Task Watchdog), surveiller les intervalles de nourrissage, détecter les retards et exposer des statistiques détaillées pour les diagnostics (JSON/API/WebSocket).

## Fonctions principales
- `WatchdogManager::begin(timeout_ms)` : configure `esp_task_wdt`, réinitialise les compteurs et active la surveillance (timeout issu de `config.advanced.watchdog_timeout_s`).
- `feed()` / `forceFeed()` : remet à zéro le watchdog tout en mettant à jour les statistiques (min/max/avg interval, `feed_count`).
- `enable()` / `disable()` : contrôle runtime du watchdog.
- `WatchdogManager::watchdogTask()` : tâche FreeRTOS dédiée à la surveillance périodique (publication Event Bus en cas d'alerte).
- Méthodes d'inspection (`getTimeSinceLastFeed`, `getTimeUntilTimeout`, `getAverageFeedInterval`, `getResetReasonString`, `checkHealth`).

## Intégration
- `feedMutex` protège l'accès concurrent depuis les tâches UART/CAN/CVL/WebSocket.
- Les valeurs sont exposées via `getStatusJSON()` (`watchdog.*`) pour l'interface web et le WebSocket.
- Publication Event Bus (`STATUS_LEVEL_WARNING/ERROR`) en cas de dérive détectée.

## Tests
- Vérifier la cohérence des compteurs via `python -m pytest tests/integration/test_end_to_end_flow.py` (statistiques dans `status_snapshot.json`).
- Prévoir un test manuel en désactivant temporairement les feeds pour confirmer le reset et la publication d'une alarme.
- Envisager un test natif simulé (mock FreeRTOS) pour valider `WatchdogManager::checkHealth()`.

## Points de vigilance
- Toujours appeler `Watchdog.begin()` après l'initialisation des mutex.
- Ne pas multiplier les feeds trop rapprochés (`validateFeedInterval()` ignore les feeds trop fréquents).
- Journaliser les échecs de feed et surveiller `getTimeUntilTimeout()` dans `/api/status`.
