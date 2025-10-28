# Module Watchdog Manager

## Rôle
Configurer et piloter le watchdog matériel de l'ESP32 (Task Watchdog), surveiller les intervalles de nourrissage, détecter les retards et exposer des statistiques pour les diagnostics.

## Fonctions principales
- `WatchdogManager::begin(timeout_ms)` : configure `esp_task_wdt`, réinitialise les compteurs et active la surveillance.
- `feed()` / `forceFeed()` : remet à zéro le watchdog tout en mettant à jour les statistiques (min/max/avg interval).
- `enable()` / `disable()` : contrôle runtime du watchdog.
- Méthodes d'inspection (`getTimeSinceLastFeed`, `getTimeUntilTimeout`, `getAverageFeedInterval`, `checkHealth`).

## Intégration
- `feedMutex` protège l'accès concurrent depuis les tâches UART/CAN/CVL/WebSocket.
- Les valeurs sont exposées via `getStatusJSON()` pour l'interface web.

## Tests
- Vérifier la cohérence des compteurs via `python -m pytest tests/integration/test_end_to_end_flow.py` (statistiques dans le snapshot).
- Prévoir un test manuel en désactivant temporairement les feeds pour confirmer le reset.

## Points de vigilance
- Toujours appeler `Watchdog.begin()` après l'initialisation des mutex.
- Ne pas multiplier les feeds trop rapprochés (`validateFeedInterval()` ignore les feeds trop fréquents).
