# Module Passerelle TinyBMS ↔ Victron (coeur)

## Rôle
Coordonner l'initialisation matérielle (UART, CAN), la configuration dynamique et la création des tâches FreeRTOS pour la passerelle `TinyBMS_Victron_Bridge`. Le module charge les paramètres depuis `ConfigManager`, prépare le HAL (`hal::HalManager`), attache la `BridgeEventSink` à l'`EventBusV2` et orchestre les intervalles de scrutation UART/CAN/CVL ainsi que le watchdog. Les autres fichiers de la passerelle (UART, CAN, CVL, keep-alive) s'appuient sur cette initialisation.

## Fichiers couverts
- `src/bridge_core.cpp`
- `src/bridge_event_sink.cpp`
- `include/bridge_core.h`
- `include/bridge_event_sink.h`

## Initialisation (`TinyBMS_Victron_Bridge::begin`)
1. Vérifie qu'un `BridgeEventSink` est attaché (`setEventSink`). L'implémentation par défaut (`defaultBridgeEventSink`) encapsule `EventBusV2` et publie directement les événements `LiveDataUpdate`, `AlarmRaised`, `WarningRaised`, `StatusMessage`, `CVLStateChanged` et `MqttRegisterValue`.
2. Récupère sous protection `configMutex` les blocs de configuration :
   - `hardware.uart` (broches, baudrate, timeout),
   - `hardware.can` (broches, bitrate, terminaison),
   - `tinybms` (polling adaptatif UART),
   - `victron` (intervalle PGN, CVL, keep-alive).
3. Configure le HAL :
   - `hal::IHalUart` via `HalManager::uart()` + `uartMutex`, avec DMA et timeout SPI (pour éviter les contentions).
   - `hal::IHalCan` via `HalManager::can()` en ajoutant un filtre standard 0x351 (`VICTRON_PGN_KEEPALIVE`).
4. Paramètre `optimization::AdaptivePolling` pour ajuster l'intervalle UART en fonction des succès/échecs (backoff/récupération).
5. Calcule les intervalles actifs : `uart_poll_interval_ms_`, `pgn_update_interval_ms_`, `cvl_update_interval_ms_`, `keepalive_interval_ms_`, `keepalive_timeout_ms_` et initialise `BridgeStats`.
6. Active la télémétrie keep-alive (`victron_keepalive_ok_ = false`, timestamps `last_keepalive_tx_ms_/rx_ms_`).

## Création des tâches (`Bridge_CreateTasks`)
- `TinyBMS_Victron_Bridge::uartTask` : scrutation Modbus TinyBMS, publication live data & alarmes.
- `TinyBMS_Victron_Bridge::canTask` : publication des PGN Victron + surveillance keep-alive.
- `TinyBMS_Victron_Bridge::cvlTask` : calcul CVL/CCL/DCL à partir du cache EventBus.
Chaque tâche est épinglée sur le cœur 1, avec des priorités `TASK_HIGH_PRIORITY` pour UART/CAN et `TASK_NORMAL_PRIORITY` pour CVL. La création ne se fait qu'après `begin()` et retourne `false` si une allocation FreeRTOS échoue.

## Synchronisation et dépendances
- Mutex globaux : `uartMutex`, `configMutex`, `feedMutex`, `statsMutex` partagés avec les autres modules.
- `WatchdogManager` : alimenté par chaque tâche via `feedMutex`.
- `BridgeEventSink` : interface polymorphe permettant de substituer la cible de publication (EventBus, tests unitaires, mocks).
- `mqtt::Publisher` : peut être injecté via `setMqttPublisher` pour propager les événements `MqttRegisterValue`.

## Statistiques (`BridgeStats`)
Le struct est réinitialisé dans le constructeur et renseigné par les tâches UART/CAN/CVL:
- Compteurs UART (`*_success_count`, `*_errors`, `*_timeouts`, `*_crc_errors`, `*_retry_count`), latence et intervalle courant.
- Compteurs CAN (TX/RX, erreurs, bus off, overflow).
- Indicateurs CVL (`cvl_state`, `cvl_current_v`, `ccl_limit_a`, `dcl_limit_a`, `cell_protection_active`).
- Télémetrie WebSocket (`websocket_sent_count`, `websocket_dropped_count`).
- Énergie (`energy_charged_wh`, `energy_discharged_wh`).
- Santé keep-alive Victron (`victron_keepalive_ok`).
Les valeurs sont exposées par `json_builders` et les routes API.

## Points d'attention
- Toujours appeler `setEventSink()` avant `begin()` (sinon l'initialisation échoue).
- `Bridge_BuildAndBegin` est le helper utilisé depuis `system_init` pour chaîner `setEventSink` + `begin`.
- Sur plateformes natives (tests), `setUart()` permet d'injecter un stub HAL UART tout en conservant les buffers circulaires.
- Le filtre CAN n'autorise que le keep-alive entrant ; les autres PGN sont émis uniquement.

## Tests recommandés
- Intégration : `python -m pytest tests/integration/test_end_to_end_flow.py` vérifie que la passerelle publie les événements attendus (`LiveDataUpdate`, alarmes) et que les compteurs sont présents dans `/api/status`.
- Tests natifs : `scripts/run_native_tests.sh` inclut les suites qui simulent le HAL UART/CAN (stubs) pour valider `begin()` en environnement PC.
- Tests manuels :
  - Forcer l'échec d'initialisation HAL (débrancher CAN ou UART) et vérifier les logs `[BRIDGE]`.
  - Simuler l'absence de keep-alive (via fixture `tests/fixtures/e2e_session.jsonl`) et contrôler la mise à jour `victron_keepalive_ok`.

## Améliorations possibles
- Mutualiser le montage SPIFFS entre Logger et Bridge pour réduire les accès redondants.
- Ajouter une instrumentation sur les temps d'initialisation (millis) pour diagnostiquer les boots lents.
- Fournir un `BridgeEventSink` de test qui accumulate les événements pour les suites unitaires natives.
