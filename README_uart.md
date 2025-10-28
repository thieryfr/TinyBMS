# Module Acquisition UART TinyBMS

## Rôle
Interroger le BMS TinyBMS via Modbus RTU sur UART, parser les réponses, mettre à jour `TinyBMS_LiveData` et publier les nouvelles mesures sur l'Event Bus.

## Principales fonctions
- `TinyBMS_Victron_Bridge::readTinyRegisters()` : gère les requêtes Modbus, les retries, la validation CRC et la copie vers les buffers de destination.
- `TinyBMS_Victron_Bridge::uartTask()` : boucle FreeRTOS qui lit les registres, alimente le watchdog et publie `EVENT_LIVE_DATA_UPDATE`.

## Synchronisation
- `uartMutex` protège l'accès au port série.
- `configMutex` est consulté pour récupérer les paramètres de retry/timeouts.
- Le watchdog est alimenté via `feedMutex` après chaque cycle réussi.

## Statistiques exposées
- `stats.uart_success_count`, `stats.uart_errors`, `stats.uart_timeouts`, `stats.uart_crc_errors`, `stats.uart_retry_count`.

## Tests
- Vérification via `python -m pytest tests/integration/test_end_to_end_flow.py` (analyse de la trace e2e).
- Prévoir un stub TinyBMS pour tests unitaires (non fourni).

## Points de vigilance
- Ne pas dépasser 256 octets de réponse (limite fixée dans la fonction).
- Toujours réinitialiser le timeout UART après tentative (`tiny_uart_.setTimeout`).
