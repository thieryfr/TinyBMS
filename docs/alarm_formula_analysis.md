# Analyse des formules d'alarmes Victron (PGN 0x35A)

## Données et seuils utilisés actuellement
- Les bits d'alarme/avertissement sont calculés dans `computeFunctionValue` à partir des grandeurs agrégées (`voltage`, `temperature`, `cell_imbalance_mv`, `soc_percent`, etc.) et des seuils configurés dans `config.victron.thresholds` (tension mini/maxi, température maxi, seuil de charge à basse température, SOC mini/maxi, seuils d'imbalance, courant de dérating). Aucun registre TinyBMS n'est consulté directement à ce stade.【F:src/bridge_can.cpp†L218-L281】
- La génération directe du PGN 0x35A applique exactement les mêmes comparaisons, avec un encodage 2 bits par condition et un résumé global en octet 7.【F:src/bridge_can.cpp†L492-L536】
- Les alarmes publiées sur l'EventBus (UART) réutilisent ces mêmes seuils Victron pour déclencher les évènements système (surtension, sous-tension, surchauffe, charge à froid, imbalance).【F:src/bridge_uart.cpp†L210-L233】
- Les seuils Victron proviennent du fichier de configuration et disposent de valeurs par défaut codées en dur (44 V, 58,4 V, 55 °C, etc.). Ils ne sont pas synchronisés automatiquement avec les registres matériels TinyBMS.【F:include/config_manager.h†L70-L80】【F:src/config_manager.cpp†L189-L207】

## État des données TinyBMS exploitables
- Le `TinyBMS_LiveData` ne stocke qu'une température pack agrégée (`Internal Temperature`, registre 48). Aucune lecture de température min/max pack (registre 113 annoncé) n'est mappée dans `tiny_read.json` ni dans les bindings `TinyRegisterRuntimeBinding` fournis par défaut.【F:src/mappings/tiny_read_mapping.cpp†L23-L55】
- Les limites d'intensité temps-réel pour la charge/décharge proviennent des registres 102 et 103, ce qui alimente la logique de dérating côté Victron. Les seuils matériels statiques (registres 317 et 318) ne sont donc pas utilisés dans les calculs actuels.【F:src/mappings/tiny_read_mapping.cpp†L36-L42】【F:src/bridge_can.cpp†L200-L281】
- Les registres de configuration 315–320 sont bien décrits dans `tiny_rw_bms.json` et exposés par l'éditeur de configuration (`TinyBMS_Config`), mais leur valeur n'est exploitée que pour l'UI/API, pas pour les algorithmes d'alarme temps-réel.【F:data/tiny_rw_bms.json†L168-L240】【F:include/tinybms_victron_bridge.h†L17-L33】【F:src/json_builders.cpp†L238-L255】

## Analyse par registre demandé
### Registre 113 – MinPackTemp / MaxPackTemp
- Pertinence : élevée pour affiner les comparaisons de température (`alarm_overtemperature`, `alarm_low_temp_charge`), qui s'appuient aujourd'hui sur la seule température interne du BMS. Utiliser la température minimale du pack garantirait que l'interdiction de charge à froid reflète bien la cellule la plus froide.
- Actions suggérées : ajouter le mapping du registre 113 (deux valeurs) dans `tiny_read.json`, propager ces mesures dans `TinyBMS_LiveData`, puis remplacer `ld.temperature` par la valeur la plus pertinente (min, max ou moyenne) dans les formules de PGN et d'alarmes.

### Registres 315 / 316 – Seuils cellule surtension / sous-tension
- Pertinence : moyenne à élevée. Les bits d'alarme 0x35A comparent actuellement la tension pack à des seuils globaux (44 V / 58,4 V par défaut). Les protections matérielles sont définies au niveau cellule en mV ; sans synchronisation, on peut signaler des alarmes trop tôt ou trop tard suivant le nombre de cellules et la configuration réelle.
- Actions suggérées : lire les registres 315/316 via l'éditeur de configuration ou directement via `TinyBMS_LiveData`, convertir en tension pack (× nombre de cellules) ou exposer la tension cellule dans le PGN si plus pertinent, puis alimenter `config.victron.thresholds` automatiquement ou utiliser ces valeurs dans `computeFunctionValue`.

### Registres 317 / 318 – Limites d'intensité surcharge/décharge
- Pertinence : faible pour les bits 0x35A (aucun bit dédié). La logique de dérating utilise déjà les limites dynamiques 102/103 fournies par le BMS. Les seuils statiques 317/318 peuvent servir de bornes max/ min pour validation ou pour l'IHM.
- Actions suggérées : conserver ces registres pour visualisation / MQTT et éventuellement vérifier que les limites temps-réel ne dépassent pas les seuils configurés, mais pas d'impact direct sur le PGN 0x35A tant qu'aucun bit correspondant n'est requis.

### Registre 319 – Seuil de coupure thermique
- Pertinence : élevée pour aligner `alarm_overtemperature` avec le comportement réel du BMS. La valeur par défaut Victron (55 °C) peut diverger du cutoff TinyBMS (par ex. 60 °C). Sans synchronisation, on risque de signaler une alarme Victron avant que la protection matérielle ne s'active (ou inversement).
- Actions suggérées : lire le registre 319, l'injecter dans `config.victron.thresholds.overtemp_c` ou comparer la température pack directement au seuil matériel plutôt qu'à une valeur configurée indépendante.

### Synthèse
- Les formules actuelles du PGN 0x35A reposent exclusivement sur des seuils logiciels Victron ; elles sont cohérentes entre CAN et EventBus mais déconnectées des paramètres TinyBMS.
- Pour garantir la pertinence des alarmes, il est recommandé d'intégrer au moins les registres 113, 315, 316 et 319 dans le pipeline de calcul et de réserver 317/318 à la supervision/visualisation.
- La mise en œuvre implique d'étendre le mapping `tiny_read.json`, d'enrichir `TinyBMS_LiveData` et/ou de synchroniser `config.victron.thresholds` avec les registres TinyBMS via l'éditeur de configuration.
