# Tableau des signaux TinyBMS ↔ Victron

| Trame / Signal | Source TinyBMS | Unité & résolution | Fréquence / déclencheur | Priorité / Notes |
|---|---|---|---|---|
| CAN 0x355 – Pack voltage | `MeasurementSample.pack_voltage_v` (UART `V=`) | 0.01 V (×100 avant envoi) | À chaque échantillon UART validé (queue) | Critique : alimente la télémétrie Victron et le calcul de puissance.【F:main/bridge.cpp†L211-L333】 |
| CAN 0x355 – Pack current | `MeasurementSample.pack_current_a` (UART `I=`) | 0.1 A (×10 avant envoi) | Identique à la tension (publication conjointe) | Critique : signe indique charge/décharge, utilisé pour limites CCL/DCL aval.【F:main/bridge.cpp†L211-L333】 |
| CAN 0x355 – State of Charge | `MeasurementSample.soc_percent` (UART `SOC=`) | 1 % (arrondi entier) | Identique à la tension (publication conjointe) | Critique : synchronise SoC Victron (PGN 0x355).【F:main/bridge.cpp†L211-L333】【F:docs/victron_register_mapping.md†L35-L45】 |
| CAN 0x355 – Temperature | `MeasurementSample.temperature_c` (UART `T=` ou défaut 25 °C) | 0.1 °C (×10) | Identique à la tension (publication conjointe) | Importante : pilote les alarmes thermique côté Victron.【F:main/bridge.cpp†L211-L333】 |
| CAN 0x351 – Keepalive | Trame fixe 0xAA55 | — | Toutes les `keepalive_period_ms` via minuterie interne | Haute priorité : maintient la session VE.Can; absence déclenche défaut Victron.【F:main/bridge.cpp†L344-L361】 |
| Diagnostics MQTT / statut | Compteurs `BridgeHealth` (`last_uart`, `last_can`, `parsed/dropped_samples`) | millisecondes depuis dernier événement, compteurs entiers | Toutes les `diagnostic_period_ms` via tâche dédiée | Moyen : supervision système, base pour topics MQTT `status`.【F:main/bridge.cpp†L367-L373】【F:main/diagnostics.cpp†L20-L73】 |

**Remarques :**
- Le taux de rafraîchissement effectif dépend de la cadence UART TinyBMS ; la queue `sample_queue_` permet de lisser jusqu’à `sample_queue_length` mesures en attente.【F:main/bridge.cpp†L256-L315】【F:main/config.cpp†L38-L41】
- Les limites courant/tension supplémentaires (CVL/CCL/DCL) décrites dans la doc Victron peuvent être ajoutées en étendant `can_task` avec de nouvelles trames (ex. 0x351/0x356) en réutilisant le pipeline existant.【F:docs/victron_register_mapping.md†L49-L66】【F:main/bridge.cpp†L308-L342】
