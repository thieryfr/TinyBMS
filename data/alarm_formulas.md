# Formules et transmission des alarmes Victron (PGN 0x35A)

## Source des données
- Les octets du PGN 0x35A sont recalculés à chaque rafraîchissement à partir des mesures `live_data_` du BMS (tension pack, courant, températures, SoC, limites dynamiques).
- Les seuils configurables proviennent de `config.victron.thresholds` protégés par `configMutex`.
- Les registres matériels TinyBMS (min/max cellule, limites d'équilibrage, coupures thermiques) sont utilisés dès qu'ils sont disponibles afin de privilégier les valeurs « terrain » au lieu de constantes.

## Transmission CAN
- `sendVictronPGN(VICTRON_PGN_ALARMS, ...)` publie la trame CAN standard 11 bits avec l'identifiant `0x35A`.
- Les compteurs `stats.can_tx_*` sont remis à jour après chaque émission pour détecter toute erreur de bus.

## Formules détaillées
| Champ | Condition(s) surveillée(s) | Détail du calcul | Pertinence |
|-------|----------------------------|------------------|------------|
| Octet 0 bits 0-1 | Sous-tension | Si les registres 316/311 sont disponibles, on compare `min_cell_mv` à `cell_undervoltage_mv`. Sinon on bascule sur la tension pack réelle `< seuil logiciel undervoltage_v`. | **Élevée** : compare en priorité les mesures cellule réelles avant de se rabattre sur le seuil Victron.|
| Octet 0 bits 2-3 | Surtension | Utilise `max_cell_mv >= cell_overvoltage_mv` lorsqu'exposé par le BMS ; à défaut `pack_voltage_v > overvoltage_v`. | **Élevée** : protège contre un emballement cellule ou une tension pack trop haute.|
| Octet 0 bits 4-5 | Surchauffe pack | `pack_temp_max_c > overheat_cutoff_c` avec fallback sur la température interne si aucune sonde pack n'est active. | **Élevée** : s'appuie sur les mesures thermiques disponibles.|
| Octet 0 bits 6-7 | Charge à froid | `pack_temp_min_c < low_temp_charge_c` *et* courant de charge `> 3 A`. | **Moyenne** : protège surtout les chimies LiFePO4 lors de charges rapides.|
| Octet 1 bits 0-1 | Déséquilibre cellules | Compare `cell_imbalance_mv` aux seuils alarme/avertissement (`imbalance_alarm_mv` / `imbalance_warn_mv`). | **Moyenne** : utile pour déclencher une surveillance manuelle du pack.|
| Octet 1 bits 2-3 | Communication / SOC / Dérating | Bit 2 passe en avertissement si `soc_percent <= soc_low_percent`. Bit 3 signale un avertissement en cas de dérating (`max_charge_current` ou `max_discharge_current` ≤ seuil) ou SOC haut (`>= soc_high_percent`). | **Moyenne** : aide au diagnostic sans couper la batterie.|
| Octet 1 bits 4-7 | (réservés) | Non utilisés actuellement. | — |
| Octet 7 bits 0-1 | Résumé global | Force le drapeau global à « alarme » si une condition critique ou une erreur de communication est active, sinon « OK ». | **Élevée** : reflète l'état général pour les appareils Victron.|

## Autres formules Victron
- **PGN 0x356 (Tension/Courant/Température)** : tension pack ×100, courant ×10 (avec signe), température interne ×10. Valeurs directement issues de `live_data_`.
- **PGN 0x355 (SoC/SoH)** : `soc_percent` et `soh_percent` multipliés par 10 pour respecter l'échelle Victron.
- **PGN 0x351 (CVL/CCL/DCL)** : utilise les limites dynamiques calculées (`stats.cvl_current_v`, `stats.ccl_limit_a`, `stats.dcl_limit_a`) avec repli sur les valeurs max autorisées par le BMS.
- Ces formules reposent toutes sur des mesures temps réel (ou valeurs limites mises à jour en continu) et restent pertinentes pour suivre l'état batterie sur GX / Cerbo.

## Recommandations
1. Continuer à privilégier les registres TinyBMS dès qu'ils sont présents afin de coller au comportement réel du pack.
2. Surveiller les seuils `VictronConfig::Thresholds` pour qu'ils reflètent la chimie et la configuration du client lorsque le fallback logiciel est utilisé.
3. Ajouter des tests d'intégration hors matériel pour valider l'encodage 2 bits et la cohérence des octets avant diffusion vers Victron.
