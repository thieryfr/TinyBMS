# Module Passerelle CAN & Keep-Alive Victron

## Rôle
Générer les trames VE.Can (PGN 0x356, 0x355, 0x351, 0x35A, 0x35E, 0x35F, 0x371, 0x378, 0x379, 0x382) à partir des mesures TinyBMS, surveiller le keep-alive (PGN 0x305) et détecter les pertes de communication Victron. Le module applique les mappings configurables (`victron_can_mapping`), publie des alarmes via l'Event Bus et maintient les compteurs d'énergie/état dans `BridgeStats`.

## Fichiers couverts
- `src/bridge_can.cpp`
- `src/bridge_keepalive.cpp`
- `include/bridge_can.h`
- `include/bridge_keepalive.h`
- `include/bridge_pgn_defs.h`
- `include/victron_can_mapping.h`

## Boucle `canTask`
1. Récupère les dernières données via `eventSink().latest()` (cache `LiveDataUpdate`).
2. Met à jour les compteurs d'énergie (`updateEnergyCounters`) en intégrant P = V × I.
3. Construit chaque PGN via `buildPGN_0x35X` en priorité avec les règles dynamiques `applyVictronMapping`; sinon fallback direct (voltage, courant, SOC, SOH, CVL/CCL/DCL, alarmes, infos fabricant, énergie cumulative, capacité installée, famille batterie).
4. Émet chaque trame avec `sendVictronPGN` (HAL CAN). Les statistiques driver sont répercutées dans `BridgeStats` (tx/rx, erreurs, bus off, dropped).
5. Exécute `keepAliveSend()` pour transmettre 0x305 selon l'intervalle configuré, et alimente le watchdog (`Watchdog.feed`).
6. `keepAliveProcessRX()` lit en boucle le driver CAN (non bloquant) pour détecter les keep-alive entrants et mettre à jour `victron_keepalive_ok` + publication `StatusMessage` / `AlarmRaised` en cas de perte.

## Mapping dynamique
- `applyVictronMapping` parcourt la définition du PGN (`VictronPgnDefinition`) : chaque champ peut provenir des live data (`TinyLiveDataField`), d'une fonction (`computeFunctionValue`) ou d'une constante.
- Les conversions (`applyConversionValue`) gèrent échelles, offsets, clamp et arrondi.
- Les fonctions personnalisées couvrent notamment :
  - `Function::CvlVoltage`/`CclLimit`/`DclLimit` (basées sur `BridgeStats` et `ConfigManager::VictronConfig::Thresholds`).
  - Statuts communication (`comm_error_cached`, `derate_cached`).
- `writeFieldValue` gère l'encodage little-endian, bits/byte, et active `wrote_any` pour savoir si le PGN est entièrement customisé.
- Si aucun mapping n'est appliqué, le code fallback encode les valeurs standard (cf. `buildPGN_0x356`, `buildPGN_0x351`, etc.).

## Gestion keep-alive (0x305)
- `keepAliveSend()` limite l'envoi selon `keepalive_interval_ms_` (configurable `config.victron.keepalive_interval_ms`).
- `keepAliveProcessRX()` incrémente `stats.can_rx_count`, met à jour `last_keepalive_rx_ms_` et déclenche :
  - `StatusMessage` niveau INFO « VE.Can keepalive OK » lors du retour en ligne.
  - `AlarmRaised` code `CanKeepAliveLost` niveau WARNING si la durée `keepalive_timeout_ms_` est dépassée.
- `stats.victron_keepalive_ok` est synchronisé avec `victron_keepalive_ok_` pour exposition REST/WebSocket.

## Interactions avec la configuration
- `TinyBMS_Victron_Bridge::begin` charge `victron.thresholds` (surcharges PGN 0x351) et `config_.battery_capacity_ah` (PGN 0x379 fallback).
- `config.logging.log_can_traffic` active la trace `[CAN] TX PGN 0x...`.
- Le mapping Victron est chargé depuis `victron_can_mapping.h` (généré à partir de `data/victron_pgn_mapping.json` si présent).

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` : vérifie la présence des PGN et compteurs dans `/api/status`, ainsi que l'alarme keep-alive via le snapshot JSON.
- Tests manuels :
  - Désactiver la réponse Victron pour observer l'alarme `VE.Can keepalive lost` et la bascule `victron_keepalive_ok` dans l'API.
  - Activer `config.logging.log_can_traffic` et contrôler les trames émises.
  - Modifier `docs/README_mapping.md`/mapping JSON et vérifier que les champs PGN sont alimentés par `applyVictronMapping`.

## Points de vigilance
- `hal::HalManager::can().receive` est appelé dans une boucle non bloquante : toujours consommer la file pour éviter les overflow.
- En cas d'échec `sendVictronPGN`, un `AlarmRaised` est publié (`CanTxError`). Penser à surveiller `stats.can_tx_errors`.
- Les chaînes (PGN 0x35E, 0x35F, 0x371, 0x382) sont assainies (`sanitize7bit`, `copyAsciiPadded`). Vérifier la taille 8 octets.
- Les compteurs d'énergie `energy_charged_wh` / `energy_discharged_wh` ne se décrémentent jamais : prévoir un reset via API si nécessaire.

## Améliorations possibles
- Exposer un endpoint de diagnostic listant les PGN customisés via mapping (et leur dernière valeur).
- Ajouter un test natif qui injecte un `VictronPgnDefinition` mocké pour valider `applyVictronMapping` sans matériel.
- Enregistrer la durée écoulée depuis le dernier keep-alive directement dans `BridgeStats` pour simplifier les dashboards.
