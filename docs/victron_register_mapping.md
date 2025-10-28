# Correspondance TinyBMS ↔ Victron CAN-BMS

Ce document centralise les liaisons entre les registres TinyBMS et les champs publiés sur le bus CAN Victron. Il résume également les facteurs d'échelle et les formules applicables afin de faciliter les audits et les vérifications croisée.

## Registres TinyBMS exploités

| Adresse | Nom TinyBMS | Type | Unité native | Notes |
| --- | --- | --- | --- | --- |
| 36 | Voltage | FLOAT | V | Tension pack instantanée. |
| 38 | Current | FLOAT | A | Courant signé (décharge < 0). |
| 40 | Min Cell | UINT16 | mV | Cellule la plus basse. |
| 41 | Max Cell | UINT16 | mV | Cellule la plus haute. |
| 45 | SOH | UINT32 | % (×10⁻⁶) | Conversion : valeur brute × 1e-6. |
| 46 | SOC | UINT32 | % (×10⁻⁶) | Conversion : valeur brute × 1e-6. |
| 48 | Temperature | INT16 | 0.1°C | Température interne TinyBMS. |
| 50 | Online Status | UINT16 | — | État fonctionnement TinyBMS. |
| 52 | Balancing Bits | UINT16 | — | Bits d'équilibrage cellules. |
| 102 | Max Discharge | UINT16 | 0.1A | Limite de décharge configurée. |
| 103 | Max Charge | UINT16 | 0.1A | Limite de charge configurée. |
| 300 | Fully Charged | UINT16 | mV | Seuil cellule pleine. |
| 315 | Overvoltage Cutoff | UINT16 | mV | Seuil coupure surtension. |

Ces définitions proviennent de `data/tinybms.js` et sont injectées dans la structure `TinyBMS_LiveData` afin d'alimenter les PGN Victron.【F:data/tinybms.js†L6-L19】【F:include/shared_data.h†L25-L71】

## PGN 0x356 — Tension / Courant / Température

| Bytes | Grandeur Victron | Source TinyBMS | Formule d'encodage |
| --- | --- | --- | --- |
| 0–1 | Tension pack (0.01 V) | Registre 36 (`voltage`) | `round(voltage × 100)` clampé sur 16 bits non signé. |
| 2–3 | Courant pack (0.1 A) | Registre 38 (`current`) | `round(current × 10)` clampé sur 16 bits signé. |
| 4–5 | Température (0.1 °C) | Registre 48 (`temperature`) | Valeur brute TinyBMS (déjà en 0.1 °C). |

Les conversions sont appliquées dans `buildPGN_0x356` avant de pousser les valeurs sur le bus CAN.【F:src/bridge_can.cpp†L444-L458】

## PGN 0x355 — SoC / SoH

| Bytes | Grandeur Victron | Source TinyBMS | Formule d'encodage |
| --- | --- | --- | --- |
| 0–1 | State of Charge (0.1 %) | Registre 46 (`soc_percent`) | `round(soc_percent × 10)` clampé sur 16 bits non signé. |
| 2–3 | State of Health (0.1 %) | Registre 45 (`soh_percent`) | `round(soh_percent × 10)` clampé sur 16 bits non signé. |

La conversion de l'échelle `×10` garantit la compatibilité avec la convention Victron détaillée dans `buildPGN_0x355`. Les valeurs `soc_percent`/`soh_percent` proviennent des registres 46 et 45 (échelle 1e-6) puis sont normalisées dans `TinyBMS_LiveData` avant encodage.【F:src/bridge_can.cpp†L460-L473】【F:include/shared_data.h†L31-L47】

## PGN 0x351 — CVL / CCL / DCL

| Bytes | Grandeur Victron | Source TinyBMS | Formule d'encodage |
| --- | --- | --- | --- |
| 0–1 | Charge Voltage Limit (0.01 V) | Algorithme CVL (`stats.cvl_current_v`) avec repli sur la tension pack (reg. 36) | `round(cvl_target_v × 100)` clampé sur 16 bits non signé. |
| 2–3 | Charge Current Limit (0.1 A) | Algorithme CVL (`stats.ccl_limit_a`) avec repli sur reg. 103 (`max_charge_current`) | `round(ccl_limit_a × 10)` clampé sur 16 bits non signé. |
| 4–5 | Discharge Current Limit (0.1 A) | Algorithme CVL (`stats.dcl_limit_a`) avec repli sur reg. 102 (`max_discharge_current`) | `round(dcl_limit_a × 10)` clampé sur 16 bits non signé. |

Le calcul `computeCvlLimits` adapte dynamiquement `cvl_current_v`, `ccl_limit_a` et `dcl_limit_a` en fonction du SoC, du déséquilibre cellule et des seuils configuration (bulk/transition/float, offsets et hystérésis). Lorsque l'algorithme est désactivé ou qu'une donnée manque, la passerelle réutilise la tension pack et les limites TinyBMS pour éviter de publier des valeurs nulles.【F:src/bridge_can.cpp†L474-L490】【F:src/cvl_logic.cpp†L15-L96】 Les limites de courant de secours proviennent des registres 102/103 exposés dans `TinyBMS_LiveData` (échelle 0.1 A).【F:include/shared_data.h†L39-L47】

## Alarmes 0x35A — Rappels de dépendances

Le PGN 0x35A construit les octets d'alarmes en fonction :

* des seuils de configuration Victron (`config.victron.thresholds`),
* des mesures pack TinyBMS (tension, température, déséquilibre cellule),
* et des registres optionnels TinyBMS (315/316/319) lorsqu'ils sont disponibles.

Les bits de gravité sont assemblés via `encode2bit` pour représenter l'état normal, warning ou alarm sur chaque condition. Voir `buildPGN_0x35A` pour le détail des comparaisons et `bridge_pgn_defs.h` pour la cartographie des bits.【F:src/bridge_can.cpp†L492-L566】【F:include/bridge_pgn_defs.h†L10-L44】

## Utilisation dans les audits

1. Vérifier que les registres TinyBMS listés ci-dessus sont bien lus dans `TinyBMS_LiveData` (via l'éditeur ou la télémétrie UART) et conservent les unités indiquées.
2. Confirmer que les conversions `×100` ou `×10` appliquées côté Victron correspondent aux résolutions attendues par les équipements GX.
3. Lors d'une incohérence (ex. CCL à zéro), contrôler l'état de l'algorithme CVL et, le cas échéant, comparer aux valeurs de repli (registres 102/103).
4. Pour les alarmes 0x35A, confronter les seuils Victron aux registres TinyBMS (315/316/319) pour assurer la cohérence des déclenchements.

Ce document est destiné à être maintenu aux côtés des structures JavaScript (`data/tinybms.js`, `data/tinybms_victron_mapping.json`) afin de garantir une vision synchronisée entre les implémentations C++ et les outils d'analyse.
