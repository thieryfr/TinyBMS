# Module Acquisition UART TinyBMS

## Rôle
Interroger le BMS TinyBMS via Modbus RTU sur UART, parser les réponses, mettre à jour `TinyBMS_LiveData`, publier les nouvelles mesures sur l'Event Bus et alimenter les statistiques de supervision (UART, watchdog, alarmes communication).

## Principales fonctions
- `TinyBMS_Victron_Bridge::readTinyRegisters()` : gère les requêtes Modbus, retries configurables (`config.tinybms.*`), validation CRC et copie vers les buffers.
- `TinyBMS_Victron_Bridge::uartTask()` : boucle FreeRTOS qui lit les registres, applique les délais `uart_poll_interval_ms_`, alimente le watchdog et publie `EVENT_LIVE_DATA_UPDATE`.
- `TinyBMS_Victron_Bridge::handleUartError()` : incrémente les compteurs d'erreurs/timeouts et déclenche des alarmes Event Bus.

## Synchronisation
- `uartMutex` protège l'accès au port série (initialisation et transactions `readTinyRegisters`).
- `configMutex` est consulté pour récupérer les paramètres de retry/timeouts et ajuster l'intervalle de polling.
- Le watchdog est alimenté via `feedMutex` après chaque cycle réussi (intégration `Watchdog.feed()`).
- Les publications Event Bus incluent `EVENT_WARNING_RAISED` en cas de perte de communication prolongée.

## Statistiques exposées
- `stats.uart_success_count`, `stats.uart_errors`, `stats.uart_timeouts`, `stats.uart_crc_errors`, `stats.uart_retry_count`.
- `stats.last_uart_ms` (timestamp), `stats.comm_lost` (flag), ainsi que les alarmes communication dans `json_builders`.

## Tests
- Vérification via `python -m pytest tests/integration/test_end_to_end_flow.py` (analyse de la trace e2e et snapshot `status`).
- Prévoir un stub TinyBMS pour tests unitaires (non fourni) afin de valider CRC/retries.
- Tester manuellement le comportement lors d'une coupure UART (vérifier les alarmes `EVENT_WARNING_RAISED`).

## Tests hors matériel
- Un stub UART TinyBMS est disponible côté natif (`tests/native/stubs/uart_stub.h`) et implémente l'interface `IUartChannel`.
- Le module `tinybms::readHoldingRegisters` encapsule les échanges Modbus et peut être exercé avec le stub.
- Exécuter `scripts/run_native_tests.sh` pour compiler et lancer les tests (`test_uart_stub`) validant les requêtes/réponses sans matériel.

## Points de vigilance
- Ne pas dépasser 256 octets de réponse (limite fixée dans la fonction).
- Toujours réinitialiser le timeout UART après tentative (`tiny_uart_.setTimeout`).
- Ajuster `config.tinybms.poll_interval_ms` pour éviter la surcharge CPU/Watchdog.
