# Module Algorithme CVL

## Rôle
Calculer la limite de tension de charge (CVL) et les limites de courant (CCL/DCL) en fonction de l'état de charge (SOC), de l'équilibrage cellules et des seuils configurés, puis publier les changements d'état sur l'Event Bus.

## Pipeline
1. `bridge_cvl.cpp` récupère périodiquement le dernier `TinyBMS_LiveData` depuis l'Event Bus.
2. `loadConfigSnapshot()` extrait un instantané de la configuration CVL/Victron sous protection `configMutex`.
3. `computeCvlLimits()` (fichier `cvl_logic.cpp`) calcule l'état suivant (BULK, TRANSITION, FLOAT_APPROACH, FLOAT, IMBALANCE_HOLD).
4. Les résultats sont stockés dans `bridge.stats` et publiés via `publishCVLStateChange` si l'état change.

## Structures clés
- `CVLInputs` : SOC, déséquilibre cellules, courant de base.
- `CVLConfigSnapshot` : seuils SOC, offsets de tension, limites d'équilibrage.
- `CVLComputationResult` : nouvel état + CVL/CCL/DCL calculés.

## Tests
- `g++ -std=c++17 -Iinclude tests/test_cvl_logic.cpp src/cvl_logic.cpp -o /tmp/test_cvl` puis `/tmp/test_cvl` pour valider les transitions.
- Tests d'intégration via `python -m pytest tests/integration/test_end_to_end_flow.py` (vérifie la présence des événements et statistiques CVL).

## Points d'amélioration
- Étendre le test natif avec des cas limites (SOC = 0 %, tensions négatives).
- Ajouter une instrumentation dans les logs pour tracer les transitions (activable via `config.logging.log_cvl_changes`).
