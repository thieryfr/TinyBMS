# Besoins MQTT TinyBMS ↔ Victron

Cette note consolide les contraintes identifiées pour la publication MQTT des données TinyBMS vers un écosystème Victron. Elle s’appuie sur la spécification de communication TinyBMS (Rev. D), la table Modbus GX officielle et les points d’extension internes de la passerelle.【F:docs/TinyBMS_Communication_Protocols_Rev_D.pdf†L1-L1】【F:docs/victron_register_mapping.md†L1-L58】【F:docs/ccgx_battery_registers.md†L1-L28】【F:docs/README_mqtt.md†L1-L60】【F:docs/mqtt_integration.md†L1-L73】

## Couverture fonctionnelle
- **Équivalence CAN ↔ MQTT** : les grandeurs publiées sur VE.Can (tension pack, courant, SOC, température, limites CVL/CCL/DCL) doivent également être exposées en MQTT pour conserver la cohérence des dashboards Victron/VRM.【F:docs/victron_register_mapping.md†L9-L49】
- **Registres système GX** : les topics doivent fournir au minimum la tension (`/Dc/Battery/Voltage`), le courant (`/Dc/Battery/Current`), la puissance (`/Dc/Battery/Power`) et l’état de charge (`/Dc/Battery/Soc`), directement alignés avec les adresses Modbus 840–843 afin de rester compatibles avec les superviseurs Victron (Node-RED, VRM, Home Assistant).【F:docs/ccgx_battery_registers.md†L1-L20】
- **Historique et alarmes** : exposer les états `LowBattery`, `HighTemperature`, etc., permet de refléter les alarmes Modbus GX et d’anticiper les états `victron_keepalive_ok` côté MQTT (utile pour les automatismes).【F:docs/ccgx_battery_registers.md†L12-L32】【F:docs/README_mqtt.md†L4-L31】

## Structure des topics et payloads
- **Hiérarchie Victron** : conserver le schéma `<root_topic>/<suffix>` où le suffixe dérive du `TinyRegisterRuntimeBinding` (clé symbolique + label). Les payloads JSON doivent inclure `address`, `value`, `raw`, `unit`, `timestamp_ms` afin de pouvoir recoller aux registres Modbus lors des audits.【F:docs/README_mqtt.md†L6-L33】
- **Synchronisation Event Bus** : la passerelle MQTT doit rester abonnée à `EVENT_MQTT_REGISTER_VALUE` et attendre la mise à jour `LIVE_DATA` (publication différée Phase 3) pour éviter les incohérences temporelles entre CAN, WebSocket et MQTT.【F:docs/mqtt_integration.md†L9-L31】
- **Granularité** : appliquer des suffixes stables (`soc_percent`, `battery_voltage`) pour faciliter l’autodiscovery Home Assistant et la corrélation avec les chemins DBus (`/Dc/Battery/*`).【F:docs/mqtt_integration.md†L32-L66】【F:docs/ccgx_battery_registers.md†L1-L20】

## Cadence et filtrage
- **Télémétrie rapide** : reprendre la fréquence UART (quelques Hz) pour tension/courant/SOC, avec possibilité de filtrage (seuil de variation) côté `publishRegister` pour ne pas saturer le broker.【F:main/bridge.cpp†L267-L342】【F:docs/README_mqtt.md†L18-L33】
- **Keepalive** : publier un heartbeat MQTT (ex. topic `status/connected=true`) calé sur l’intervalle CAN `keepalive_period_ms` afin d’aligner la supervision réseau et VE.Can.【F:main/bridge.cpp†L344-L361】【F:docs/README_mqtt.md†L18-L33】

## Fiabilité et sécurité
- **QoS** : utiliser QoS 1 pour les états critiques (limites courant/tension, alarmes) et QoS 0 pour la télémétrie rapide, conformément à la configuration `default_qos`/overrides définie dans `BrokerSettings`.【F:docs/README_mqtt.md†L18-L42】
- **Reconnexion** : implémenter `shouldAttemptReconnect` et surveiller `connected_`/`connecting_` pour garantir une reprise automatique similaire au comportement VE.Can (watchdog).【F:docs/README_mqtt.md†L24-L34】
- **TLS & credentials** : prévoir TLS, identifiant client stable et credentials optionnels (broker local/VRM) comme exposé dans `BrokerSettings`. Ceci est nécessaire pour les déploiements cloud VRM et la conformité IT industrielle.【F:docs/mqtt_integration.md†L33-L73】

## Observabilité
- **Compteurs diagnostics** : propager `publish_count`, `failed_publish_count`, `last_error_code` dans `/api/status` et, si possible, les republier sur un topic dédié (ex. `<root_topic>/status`) pour refléter l’équivalent MQTT de `BridgeHealth`.【F:docs/README_mqtt.md†L18-L38】【F:main/diagnostics.cpp†L20-L73】
- **Alignement Modbus** : ajouter à chaque payload l’adresse Modbus GX (quand disponible) pour faciliter le cross-check avec les outils Victron (ModbusTCP, Venus OS).【F:docs/ccgx_battery_registers.md†L1-L32】

## Extensions suggérées
- **Mode discovery Victron/VRM** : publier un manifest JSON (liste des topics ↔ registres) pour automatiser la configuration des superviseurs utilisant le fichier Modbus officiel.
- **Bufferisation hors-ligne** : en cas de coupure broker, conserver les mesures critiques (SoC, limites courant) pour les rejouer à la reconnexion (persistance locale).

Ces exigences garantissent que la couche MQTT restitue fidèlement les attentes Victron/TinyBMS tout en restant alignée avec l’architecture interne (Event Bus, diagnostics, cadence CAN).
