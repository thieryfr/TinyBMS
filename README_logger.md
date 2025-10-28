# Module Logger

## Rôle
Assurer la journalisation centralisée avec niveaux configurables, sortie Serial et persistance SPIFFS avec rotation.

## Capacités
- `Logger::begin()` : charge le niveau depuis `ConfigManager`, monte SPIFFS et ouvre `/logs.txt` en mode append.
- `log()` : formate les messages (timestamp FreeRTOS, niveau) et les écrit sur Serial + SPIFFS.
- `rotateLogFile()` : redémarre le fichier lorsque la taille dépasse 100 Ko.
- `setLogLevel()` / `getLevel()` : ajustent dynamiquement la verbosité.
- `getLogs()` / `clearLogs()` : exposent la récupération/suppression pour l'API web.

## Synchronisation
- Mutex interne `log_mutex_` pour sérialiser l'accès fichier.
- Dépend de `configMutex` pour initialiser le niveau.

## Tests
- Vérifier la configuration via `python -m pytest tests/integration/test_end_to_end_flow.py` (statut du logger dans snapshot).
- Prévoir un test manuel de rotation en injectant des logs massifs.

## Bonnes pratiques
- Mutualiser l'appel à `SPIFFS.begin()` avec le ConfigManager lors d'une refonte.
- Limiter les logs `DEBUG` en production pour préserver la durée de vie de la flash.
