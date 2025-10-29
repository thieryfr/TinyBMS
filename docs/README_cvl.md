# Module Algorithme CVL

## Rôle
Calculer la limite de tension de charge (CVL) et les limites de courant (CCL/DCL) en fonction de l'état de charge (SOC), du courant pack, de l'équilibrage cellules et des seuils configurés. Le module introduit un mode « Sustain » bas voltage/courant pour les batteries vides, applique une hystérésis explicite sur les déséquilibres cellules et protège la tension CVL en fonction de la cellule la plus haute et du courant de charge. Les transitions publiées via l'Event Bus mettent à jour `bridge.stats` pour la télémétrie CAN.

## Pipeline
1. `bridge_cvl.cpp` récupère périodiquement le dernier `TinyBMS_LiveData` depuis l'Event Bus (cache `getLatestLiveData`).
2. `loadConfigSnapshot()` extrait un instantané de la configuration CVL/Victron sous protection `configMutex` (seuils SOC, offsets, hystérésis cellule, mode Sustain, limites CCL/DCL minimales).
3. `computeCvlLimits()` (fichier `cvl_logic.cpp`) calcule l'état suivant (`BULK`, `TRANSITION`, `FLOAT_APPROACH`, `FLOAT`, `IMBALANCE_HOLD`, `SUSTAIN`) à partir des entrées courantes et de `CVLRuntimeState` (cvl précédent + drapeaux de protection) et renvoie les valeurs CVL/CCL/DCL.
4. `applyCvlResult()` met à jour `bridge.stats`, publie `EVENT_CVL_STATE_CHANGED` et optionnellement logge la transition selon `config.logging.log_cvl_changes`.

## Structures clés
- `CVLInputs` : SOC, déséquilibre cellules, tension pack et cellule max, courant pack (charge), limites CCL/DCL issues du BMS.
- `CVLConfigSnapshot` : seuils SOC (bulk, transition, float, sustain), offsets tension, hystérésis cellule, limites CCL/DCL minimales, paramètres d'équilibrage et de protection cellule.
- `CvlTaskContext` : accumulateur d'état, timestamps, détection d'imbalance prolongée.
- `CVLRuntimeState` : dernier état publié, CVL précédent et drapeau de protection cellule.
- `CVLComputationResult` : nouvel état + CVL/CCL/DCL calculés, drapeaux `imbalance_hold_active` et `cell_protection_active`.

## Diagramme UML

```plantuml
@startuml
class CVLInputs {
  +soc_percent: float
  +cell_imbalance_mv: uint16_t
  +pack_voltage_v: float
  +base_ccl_limit_a: float
  +base_dcl_limit_a: float
  +pack_current_a: float
  +max_cell_voltage_v: float
}

class CVLConfigSnapshot {
  +enabled: bool
  +bulk_soc_threshold: float
  +transition_soc_threshold: float
  +float_soc_threshold: float
  +float_exit_soc: float
  +float_approach_offset_mv: float
  +float_offset_mv: float
  +minimum_ccl_in_float_a: float
  +imbalance_hold_threshold_mv: uint16_t
  +imbalance_release_threshold_mv: uint16_t
  +bulk_target_voltage_v: float
  +series_cell_count: uint16_t
  +cell_max_voltage_v: float
  +cell_safety_threshold_v: float
  +cell_safety_release_v: float
  +cell_min_float_voltage_v: float
  +cell_protection_kp: float
  +dynamic_current_nominal_a: float
  +max_recovery_step_v: float
  +sustain_soc_entry_percent: float
  +sustain_soc_exit_percent: float
  +sustain_voltage_v: float
  +sustain_per_cell_voltage_v: float
  +sustain_ccl_limit_a: float
  +sustain_dcl_limit_a: float
  +imbalance_drop_per_mv: float
  +imbalance_drop_max_v: float
}

class CVLComputationResult {
  +state: CVLState
  +cvl_voltage_v: float
  +ccl_limit_a: float
  +dcl_limit_a: float
  +imbalance_hold_active: bool
  +cell_protection_active: bool
}

class «function» computeCvlLimits {
  +operator()(CVLInputs, CVLConfigSnapshot, CVLRuntimeState): CVLComputationResult
}

class TinyBMS_Victron_Bridge {
  +cvlTask(void*): void
  -last_cvl_update_ms_: uint32_t
  -cvl_update_interval_ms_: uint32_t
  +stats: BridgeStats
}

class ConfigManager {
  +cvl: CVLConfig
  +victron: VictronConfig
  +logging: LoggingConfig
}

class EventBus {
  +getLatestLiveData(out TinyBMS_LiveData): bool
  +publishCVLStateChange(...): bool
}

class Logger {
  +log(level, message): void
}

class WatchdogManager {
  +feed(): void
}

class CVL_StateChange {
  +old_state: uint8_t
  +new_state: uint8_t
  +new_cvl_voltage: float
  +new_ccl_current: float
  +new_dcl_current: float
  +state_duration_ms: uint32_t
}

CVLInputs --> computeCvlLimits
CVLConfigSnapshot --> computeCvlLimits
computeCvlLimits --> CVLComputationResult

TinyBMS_Victron_Bridge --> CVLInputs : construit
TinyBMS_Victron_Bridge --> CVLConfigSnapshot : construit via loadConfigSnapshot
TinyBMS_Victron_Bridge --> computeCvlLimits : appelle
TinyBMS_Victron_Bridge --> EventBus : getLatestLiveData()/publishCVLStateChange()
TinyBMS_Victron_Bridge --> Logger : log()
TinyBMS_Victron_Bridge --> WatchdogManager : feed()

ConfigManager --> CVLConfig
ConfigManager --> VictronConfig
ConfigManager --> LoggingConfig
EventBus --> CVL_StateChange : publie

@enduml
```

## États et protections

- **Bulk / Transition / Float approach / Float** : états SOC traditionnels s'appuyant sur les seuils configurés et l'hystérésis `float_exit_soc`. En `FLOAT` la CCL peut être bridée via `minimum_ccl_in_float_a`.
- **Imbalance hold** : déclenché lorsque `cell_imbalance_mv` dépasse `imbalance_hold_threshold_mv`. Le CVL est réduit selon une pente `imbalance_drop_per_mv` plafonnée à `imbalance_drop_max_v` et ne remonte qu'après retour sous `imbalance_release_threshold_mv`.
- **Sustain** : activé quand le SOC passe sous `sustain_soc_entry_percent`. La tension CVL tombe au minimum (valeur fixe `sustain_voltage_v` ou calculée par cellule) et les courants CCL/DCL sont bridés (`sustain_ccl_limit_a` / `sustain_dcl_limit_a`). La sortie nécessite de repasser au-dessus de `sustain_soc_exit_percent`.
- **Protection cellule** : si la cellule la plus haute franchit `cell_safety_threshold_v`, une réduction dynamique est appliquée sur la tension pack (`cell_protection_kp` × courant relatif). La récupération est plafonnée par `max_recovery_step_v` et reste bornée par `cell_min_float_voltage_v`.

## Tests
- `g++ -std=c++17 -Iinclude tests/test_cvl_logic.cpp src/cvl_logic.cpp -o /tmp/test_cvl` puis `/tmp/test_cvl` pour valider les transitions, la protection cellule et le mode Sustain.
- Tests d'intégration via `python -m pytest tests/integration/test_end_to_end_flow.py` (vérifie la présence des événements et statistiques CVL dans `status_snapshot.json`).
- Vérification manuelle des logs si `config.logging.log_cvl_changes` est activé.

## Points d'amélioration
- Étendre le test natif avec des cas limites (SOC = 0 %, imbalance extrême, CVL désactivé).
- Ajouter une instrumentation dans les logs pour tracer les transitions (activable via `config.logging.log_cvl_changes`).
- Prévoir une validation croisée avec la télémétrie CAN (PGN 0x351) pour garantir la cohérence des limites envoyées.
