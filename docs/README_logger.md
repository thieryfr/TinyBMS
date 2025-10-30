# Module Logger

## Rôle
Fournir une journalisation centralisée avec niveaux configurables, sortie Serial et persistance via l'abstraction de stockage du HAL (`HalManager::storage()`). Les logs sont accessibles depuis l'API web et peuvent être effacés ou téléchargés pour le diagnostic.

## API & flux principaux
- `Logger::begin(config)` lit `config.logging.log_level` sous protection `configMutex`, initialise le niveau actif et ouvre `/logs.txt` via `hal::IHalStorage` (`Append`).
- `Logger::log(level, message)` formate un message `[millis][LEVEL]` puis l'écrit sur Serial et dans le fichier, en protégeant l'accès avec `log_mutex_`. Une rotation est effectuée au-delà de 100 Ko (`rotateLogFile`).
- `Logger::setLogLevel()` / `Logger::getLevel()` permettent d'ajuster dynamiquement la verbosité (routes REST `/api/logs/level`).
- `Logger::getLogs()` relit `/logs.txt` via le HAL pour exposition `/api/logs/download`; `clearLogs()` recrée le fichier proprement.

## Synchronisation & dépendances
- Mutex internes : `log_mutex_` sérialise les écritures/rotations et les lectures API.
- Dépendances globales : `configMutex` pour récupérer la configuration initiale, `hal::HalManager::storage()` pour l'accès fichier.
- Aucun montage SPIFFS direct dans le logger : le choix du backend (SPIFFS/NVS) est réalisé dans `system_init` en configurant le HAL (`buildHalConfig`).

## Intégrations
- Les modules bridge utilisent le macro `BRIDGE_LOG` pour produire des traces `[UART]`, `[CAN]`, `[BRIDGE]`, etc.
- Les routes web (`web_routes_api.cpp`) exposent `/api/logs/download`, `/api/logs/clear`, `/api/logs/level` et reflètent l'état courant dans `/api/status` (`logging.log_level`).
- Les options avancées `config.logging.log_can_traffic`, `log_uart_traffic`, `log_cvl_changes` sont lues par les modules correspondants pour activer des traces détaillées.

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` vérifie la présence des messages logger et le paramétrage `logging` dans les snapshots.
- Tests manuels :
  - Forcer l'écriture massive (ex. activer `log_can_traffic`) pour valider la rotation 100 Ko.
  - Consulter `/api/logs/download` et `/api/logs/clear` pour confirmer la lecture/effacement via le HAL.
  - Modifier dynamiquement le niveau via `/api/logs/level` et vérifier l'effet immédiat sur la sortie Serial.

## Bonnes pratiques
- Conserver `log_mutex_` sur toute opération lourde (lecture complète, rotation) afin d'éviter les corruptions.
- Surveiller la taille de `/logs.txt` dans les diagnostics : un log DEBUG prolongé peut saturer le stockage flash.
- Mutualiser les accès storage entre Logger et ConfigManager (déjà via HAL) pour limiter les montages multiples.
