# Module Passerelle CAN & Keep-Alive Victron

## Rôle
Publier les PGN VE.Can (0x356, 0x355, 0x351, 0x35A, 0x35E, 0x35F, 0x371, 0x378, 0x379, 0x382) à partir des données TinyBMS, surveiller le keep-alive 0x305 et remonter l'état du bus CAN. Le module applique les mappings dynamiques (`victron_can_mapping`), maintient les compteurs d'énergie et alimente les statistiques exposées via l'API.

## Fichiers couverts
- `src/bridge_can.cpp`
- `src/bridge_keepalive.cpp`
- `include/bridge_can.h`
- `include/bridge_keepalive.h`
- `include/bridge_pgn_defs.h`
- `include/victron_can_mapping.h`

## Boucle `canTask`
1. Récupère le dernier `LiveDataUpdate` via `BridgeEventSink::latest` (cache Event Bus).
2. Met à jour les compteurs d'énergie (`updateEnergyCounters`) en intégrant `P = V × I`.
3. Construit chaque PGN via `buildPGN_0x35X` : priorité au mapping `VictronPgnDefinition` (chargé depuis SPIFFS), sinon fallback « valeurs natives » (tension, courant, SOC/SOH, limites CVL, infos fabricant, énergie cumulée, capacité, famille batterie).
4. Émet les trames via `sendVictronPGN` (HAL CAN) et journalise éventuellement le trafic si `config.logging.log_can_traffic` est actif.
5. Alimente le watchdog (`Watchdog.feed()` sous `feedMutex`).
6. Rafraîchit les statistiques driver (`hal::IHalCan::getStats`) sous `statsMutex` (tx, erreurs, bus off, overflow).

## Mapping dynamique
- `applyVictronMapping` parcourt chaque champ (`VictronPgnDefinition::fields`), résout la source (`TinyLiveDataField`, constantes, fonctions personnalisées) et applique les conversions (`scale`, `offset`, clamp, arrondi).
- Les fonctions dérivées couvrent CVL/CCL/DCL, états de communication (`victron_keepalive_ok`), dérating courant, etc.
- Les chaînes (PGN 0x35E/0x35F/0x371/0x382) utilisent `sanitize7bit` et `copyAsciiPadded` pour respecter l'encodage 7 bits.

## Gestion keep-alive (0x305)
- `keepAliveSend()` cadence l'émission selon `keepalive_interval_ms_`.
- `keepAliveProcessRX()` lit le driver CAN (non bloquant) pour détecter les trames entrantes :
  - Passage à `victron_keepalive_ok=true` + publication `StatusMessage` « VE.Can keepalive OK » lors du retour en ligne.
  - Publication `AlarmRaised` (`AlarmCode::CanKeepAliveLost`) en cas de dépassement `keepalive_timeout_ms_`.
  - `stats.victron_keepalive_ok` reflète l'état courant pour les API.

## Interactions configuration
- `TinyBMS_Victron_Bridge::begin()` charge `config.victron` (seuils, intervalles, manufacturer/battery name) et `config_.battery_capacity_ah` (fallback PGN 0x379).
- `initializeVictronCanMapping()` lit `/tiny_read_4vic.json` depuis SPIFFS, permettant de personnaliser les PGN sans modifier le firmware.
- Les compteurs d'énergie (`energy_charged_wh`, `energy_discharged_wh`) sont persistés dans `BridgeStats` (reset via API si besoin).

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` valide la présence des PGN dans `/api/status`, la mise à jour `victron_keepalive_ok` et l'exposition des stats CAN.
- Tests manuels :
  - Couper la réponse Victron pour vérifier l'alarme `VE.Can keepalive lost` et l'indicateur API.
  - Activer `config.logging.log_can_traffic` pour inspecter les trames émises.
  - Modifier le mapping `/tiny_read_4vic.json` puis redémarrer pour vérifier l'injection des champs personnalisés.

## Points de vigilance
- Toujours consommer la file CAN (`keepAliveProcessRX`) pour éviter les overflow driver.
- Surveiller `stats.can_tx_errors`/`stats.can_bus_off_count` en cas de câblage défectueux.
- Les chaînes Victron doivent rester à 8 octets (padding zéro).
