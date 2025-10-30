# Module Acquisition UART TinyBMS

## Rôle
Interroger le TinyBMS via Modbus RTU, mettre à jour `TinyBMS_LiveData`, alimenter les statistiques UART et publier les événements (`LiveDataUpdate`, `MqttRegisterValue`, alarmes) sur l'Event Bus. La tâche `uartTask` pilote aussi l'adaptation de cadence (`AdaptivePolling`) et nourrit le watchdog.

## Flux principal (`uartTask`)
1. Pour chaque bloc défini dans `kTinyReadBlocks`, `readTinyRegisters()` exécute une requête Modbus (avec retries configurables) via `hal::IHalUart` protégé par `uartMutex`.
2. Les mots reçus sont stockés dans `register_values` puis transformés en `TinyBMS_LiveData` via `tinybms::uart::detail::decodeAndApplyBinding`.
3. Les événements MQTT sont collectés (payload `MqttRegisterEvent`) puis publiés après le `LiveDataUpdate` pour garantir que les consommateurs disposent d'un snapshot cohérent.
4. Les seuils TinyBMS (OV/UV/OC, températures) actualisent `bridge.config_` afin d'alimenter les PGN et les diagnostics.
5. Des alarmes `AlarmRaised` sont émises selon les seuils Victron (`config.victron.thresholds`) ou les limites TinyBMS (OV, UV, imbalance, températures, charge à froid, échec lecture).
6. Le watchdog est nourri en fin de cycle et la tâche dort `uart_poll_interval_ms_` (piloté par `AdaptivePoller`).

## Statistiques & adaptation
- `readTinyRegisters()` incrémente `stats.uart_errors`, `stats.uart_timeouts`, `stats.uart_crc_errors`, `stats.uart_retry_count` et renseigne la latence (`uart_latency_last_ms`, `uart_latency_max_ms`, `uart_latency_avg_ms`) sous protection `statsMutex`.
- `AdaptivePoller` ajuste `uart_poll_interval_ms_` à partir des succès/échecs (`poll_failure_threshold`, `poll_success_threshold`) et expose la valeur courante via `stats.uart_poll_interval_current_ms`.
- Les succès modifient `stats.uart_success_count` tandis que les échecs publient une alarme `AlarmCode::UartError`.

## Synchronisation
- `uartMutex` protège l'accès au HAL UART (écritures/lectures Modbus).
- `configMutex` est utilisé pour lire les seuils Victron avant d'évaluer les alarmes.
- `feedMutex` synchronise l'appel `Watchdog.feed()`.
- `statsMutex` évite les courses lors de la mise à jour des compteurs partagés (exposés via `/api/status`).

## Mapping dynamique
- Les bindings (`tiny_read_mapping`) sont chargés dans `initializeSystem()` et consultés dans `uartTask` pour construire dynamiquement les snapshots `registers[]`.
- `TinyBMSConfigEditor` et l'API Web réutilisent ces métadonnées pour présenter les registres lisibles/écrits.

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` couvre le flux complet : lecture UART simulée, publication Event Bus, présence des stats et alarmes dans `/api/status`.
- Tests manuels recommandés :
  - Débrancher le TinyBMS pour vérifier la remontée de l'alarme `TinyBMS UART error` et l'incrément `stats.uart_errors`.
  - Activer `config.logging.log_uart_traffic` pour inspecter le trafic Modbus et valider les timings adaptatifs.
  - Vérifier que les registres textes (Manufacturer/Family) apparaissent dans l'API après chargement du mapping.
