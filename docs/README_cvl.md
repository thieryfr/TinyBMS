# Module Algorithme CVL

## Rôle
Calculer la limite de tension de charge (CVL) et les limites de courant (CCL/DCL) en fonction de l'état de charge (SOC), de l'équilibrage cellules, des seuils configurés et des paramètres d'hystérésis. Publie les changements d'état sur l'Event Bus et met à jour `bridge.stats` pour la télémétrie CAN.

## Pipeline
1. `bridge_cvl.cpp` récupère périodiquement le dernier `TinyBMS_LiveData` depuis l'Event Bus (cache `getLatestLiveData`).
2. `loadConfigSnapshot()` extrait un instantané de la configuration CVL/Victron sous protection `configMutex` (seuils SOC, offsets, limites CCL/DCL minimales).
3. `computeCvlLimits()` (fichier `cvl_logic.cpp`) calcule l'état suivant (`BULK`, `TRANSITION`, `FLOAT_APPROACH`, `FLOAT`, `IMBALANCE_HOLD`) et les valeurs CVL/CCL/DCL.
4. `applyCvlResult()` met à jour `bridge.stats`, publie `EVENT_CVL_STATE_CHANGED` et optionnellement logge la transition selon `config.logging.log_cvl_changes`.

## Structures clés
- `CVLInputs` : SOC, déséquilibre cellules, courant de base.
- `CVLConfigSnapshot` : seuils SOC (bulk, transition, float), offsets tension, hystérésis, limites CCL/DCL minimales, seuils d'imbalance.
- `CvlTaskContext` : accumulateur d'état, timestamps, détection d'imbalance prolongée.
- `CVLComputationResult` : nouvel état + CVL/CCL/DCL calculés.

## Tests
- `g++ -std=c++17 -Iinclude tests/test_cvl_logic.cpp src/cvl_logic.cpp -o /tmp/test_cvl` puis `/tmp/test_cvl` pour valider les transitions et l'hystérésis.
- Tests d'intégration via `python -m pytest tests/integration/test_end_to_end_flow.py` (vérifie la présence des événements et statistiques CVL dans `status_snapshot.json`).
- Vérification manuelle des logs si `config.logging.log_cvl_changes` est activé.

## Points d'amélioration
- Étendre le test natif avec des cas limites (SOC = 0 %, imbalance extrême, CVL désactivé).
- Ajouter une instrumentation dans les logs pour tracer les transitions (activable via `config.logging.log_cvl_changes`).
- Prévoir une validation croisée avec la télémétrie CAN (PGN 0x351) pour garantir la cohérence des limites envoyées.
