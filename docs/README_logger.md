# Module Logger

## Rôle
Assurer la journalisation centralisée avec niveaux configurables, sortie Serial et persistance SPIFFS avec rotation. Fournit des API pour consulter/purger les logs via l'interface web et expose des options d'activation du traçage UART/CAN/CVL.

## Capacités
- `Logger::begin()` : charge le niveau depuis `ConfigManager`, monte SPIFFS, ouvre `/logs.txt` (append) et respecte le débit `config.logging.serial_baudrate`.
- `log()` : formate les messages (timestamp FreeRTOS, niveau) et les écrit sur Serial + SPIFFS (mutex + flush immédiat).
- `rotateLogFile()` : redémarre le fichier lorsque la taille dépasse 100 Ko.
- `setLogLevel()` / `getLevel()` : ajustent dynamiquement la verbosité (via API REST ou configuration).
- `getLogs()` / `clearLogs()` : exposent la récupération/suppression pour l'API web.
- Options additionnelles : `config.logging.log_can_traffic`, `log_uart_traffic`, `log_cvl_changes` pour activer les traces détaillées.

## Synchronisation
- Mutex interne `log_mutex_` pour sérialiser l'accès fichier et les opérations rotation/lecture.
- Dépend de `configMutex` pour initialiser le niveau et les flags de logging avancé.
- SPIFFS est monté dans `Logger::begin()` ; envisager une mutualisation avec `ConfigManager` pour éviter les doubles montages.

## Tests
- Vérifier la configuration via `python -m pytest tests/integration/test_end_to_end_flow.py` (`status_snapshot.json` contient la section `stats.event_bus` + messages de statut logger).
- Prévoir un test manuel de rotation en injectant des logs massifs ou via un script Python (`/api/logs`).
- Ajouter un test unitaire natif simulant `log_can_traffic` pour s'assurer de l'impact mémoire.

## Bonnes pratiques
- Mutualiser l'appel à `SPIFFS.begin()` avec le ConfigManager lors d'une refonte.
- Limiter les logs `DEBUG` en production pour préserver la durée de vie de la flash.
- Implémenter un seuil d'alerte si `log_file_.size()` approche de la limite (exposition via `/api/status`).
