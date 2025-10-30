# Intégration MQTT

Cette note décrit les nouveaux points d'extension autour de la lecture/écriture des registres TinyBMS et la stratégie retenue pour publier ces valeurs via MQTT.

Comme les autres modules, MQTT module doit impérativement publier et consommer via ce bus interne d'evenements event_bus afin de préserver l'architecture découplée existante et éviter les accès directs concurrents aux registres TinyBMS.

## Points d'extension autour des registres

* `TinyBMS_LiveData::appendSnapshot` continue de stocker toutes les mesures décodées depuis les trames Modbus TinyBMS. Chaque instantané contient la valeur brute (`raw_words`), la valeur numérique décodée et, le cas échéant, une chaîne.
* `TinyBMS_Victron_Bridge::uartTask` applique désormais `mqtt::buildRegisterValue` juste après `TinyBMS_LiveData::applyBinding`. Cette étape transforme la valeur Modbus (brute + échelle `TinyRegisterRuntimeBinding`) en structure `mqtt::RegisterValue` prête à être publiée vers un backend.
* `TinyBMS_Victron_Bridge::setMqttPublisher` permet d'enregistrer une implémentation de `mqtt::Publisher` qui recevra toutes les mesures TinyBMS au fil des acquisitions UART.
* Le bus d'évènements expose désormais `EVENT_MQTT_REGISTER_VALUE` contenant les échantillons modbus prêts à être transformés en messages MQTT. `mqtt::VictronMqttBridge` s'y abonne et reconstruit les métadonnées via `mqtt::buildRegisterValue` avant de publier vers le broker.

## Interface `mqtt::Publisher`

L'interface se trouve dans `include/mqtt/publisher.h` et expose trois briques principales :

1. `BrokerSettings` décrit la configuration de base du client MQTT (URI, identifiants, QoS par défaut, stratégie de reconnexion, TLS…).
2. `RegisterValue` encapsule la valeur d'un registre TinyBMS : adresse, clef symbolique, unité, valeur numérique mise à l'échelle, valeur brute, valeur texte éventuelle, précision suggérée et suffixe de topic.
3. `Publisher` définit le cycle de vie minimal (`configure`, `connect`, `disconnect`, `loop`) ainsi que `publishRegister`, appelée pour chaque valeur TinyBMS. Les implémentations peuvent surcharger QoS/retain par publication.

La fonction utilitaire `mqtt::buildRegisterValue` regroupe la logique de conversion : elle combine les métadonnées `tiny_read_mapping` (nom, unité) et `tiny_rw_mapping` (classe de valeur, valeur par défaut, précision) afin de fournir une vue homogène à l'éditeur MQTT.

## Choix de la bibliothèque MQTT

L'intégration cible l'ESP32 ; la bibliothèque retenue est donc l'API officielle ESP-IDF `esp_mqtt_client`. Elle offre :

* Gestion native du réseau FreeRTOS/ESP-IDF déjà utilisée par le projet.
* Support complet du QoS 0/1/2, TLS et authentification username/password.
* Callbacks de connexion/déconnexion compatibles avec la boucle principale (`loop`).

### Cycle de vie

1. `configure()` prépare une structure `esp_mqtt_client_config_t` à partir de `BrokerSettings` (URI, certificats, keepalive, identifiants).
2. `connect()` instancie le client via `esp_mqtt_client_init` puis lance `esp_mqtt_client_start`. Une tâche interne gère les reconnexions automatiques ; `reconnect_interval_ms` peut piloter une temporisation supplémentaire côté application.
3. `loop()` permet de traiter les callbacks éventuels (selon l'implémentation, par exemple vidage d'une file d'attente vers `esp_mqtt_client_publish`).
4. `disconnect()` appelle `esp_mqtt_client_stop` puis `esp_mqtt_client_destroy` pour libérer les ressources.

Une implémentation typique peut stocker la dernière configuration, surveiller `isConnected()` via les événements `MQTT_EVENT_CONNECTED/DISCONNECTED` et retenter une connexion manuelle lorsque le client reste inactif plus de `reconnect_interval_ms`.

### Politique QoS/retain

* Par défaut, `BrokerSettings::default_qos` et `BrokerSettings::retain_by_default` définissent les paramètres appliqués aux publications TinyBMS.
* `publishRegister` accepte des overrides ponctuels pour fournir un QoS différent selon le type de mesure (ex. QoS 1 pour les alertes critiques, QoS 0 pour la télémétrie fréquente).
* Les implémentations peuvent appliquer un filtrage (ex. ne publier que les variations supérieures à un seuil) avant d'appeler `esp_mqtt_client_publish`.

## Configuration attendue

Configurer un backend MQTT nécessite les éléments suivants :

| Champ | Description | Exemple |
|-------|-------------|---------|
| `uri` | URI du broker (`mqtt://`, `mqtts://`) | `mqtts://broker.local` |
| `port` | Port TCP | `8883` |
| `client_id` | Identifiant unique du client ESP32 | `tinybms-gateway-01` |
| `username` / `password` | Identifiants optionnels | `tinybms` / `S3cret!` |
| `root_topic` | Préfixe pour les mesures TinyBMS | `home/garage/battery` |
| `default_qos` | QoS par défaut (0/1/2) | `1` |
| `retain_by_default` | Retain pour les mesures stables | `true` |
| `keepalive_seconds` | Intervalle keepalive MQTT | `30` |
| `reconnect_interval_ms` | Délai avant reconnection manuelle | `5000` |
| `use_tls` | Active TLS | `true` |
| `server_certificate` | Certificat CA PEM (si TLS) | `-----BEGIN CERTIFICATE----- ...` |

### Topics

Le suffixe de topic est généré automatiquement à partir de la clef `TinyBMS` (ex. `soc_percent`), du libellé (`Battery Pack Voltage`) ou, par défaut, de l'adresse (`36`). Le topic final suit le schéma :

```
<root_topic>/<topic_suffix>
```

Exemple : `home/garage/battery/soc_percent`.

## Table de correspondance Victron ↔ MQTT

| PGN / champ Victron | Topic MQTT (suffixe) | Source TinyBMS | Factorisation & bornes |
| --- | --- | --- | --- |
| 0x356 / BatteryVoltage | `battery_pack_voltage` | Registre 36 – « Battery Pack Voltage » | ×100 pour produire 0,01 V, borné à 0–65535 afin de respecter le registre 16 bits non signé.【F:data/tiny_read_4vic.json†L5-L35】【F:legacy/arduino_src/mappings/tiny_read_mapping.cpp†L36-L44】 |
| 0x356 / BatteryCurrent | `battery_pack_current` | Registre 38 – « Battery Pack Current » | ×10 pour 0,1 A, clampé à −32768..32767 (signed 16 bits) conformément au profil Victron.【F:data/tiny_read_4vic.json†L18-L25】【F:legacy/arduino_src/mappings/tiny_read_mapping.cpp†L36-L44】 |
| 0x356 / BatteryTemperature | `internal_temperature` | Registre 48 – « Internal Temperature » | Valeur TinyBMS déjà en 0,1 °C, publiée telle quelle (gain 1) sur 16 bits signés.【F:data/tiny_read_4vic.json†L26-L34】【F:legacy/arduino_src/mappings/tiny_read_mapping.cpp†L44-L52】 |
| 0x355 / StateOfCharge | `state_of_charge` | Registre 46 – `soc_percent` mis à l’échelle | ×10 pour obtenir 0,1 %, borné à 0–65535 conformément au PGN Victron.【F:data/tiny_read_4vic.json†L38-L58】【F:docs/victron_register_mapping.md†L35-L42】 |
| 0x355 / StateOfHealth | `state_of_health` | Registre 45 – `soh_percent` mis à l’échelle | ×10 pour 0,1 %, borné à 0–65535 identique au SoC.【F:data/tiny_read_4vic.json†L38-L58】【F:docs/victron_register_mapping.md†L35-L42】 |
| 0x351 / ChargeVoltageLimit | `charge_voltage_limit` | Calcul CVL (`stats.cvl_current_v`) avec repli sur reg. 36 | ×100 (0,01 V) sur 16 bits non signés pour rester dans la fenêtre Victron.【F:data/tiny_read_4vic.json†L61-L92】【F:docs/victron_register_mapping.md†L44-L52】 |
| 0x351 / ChargeCurrentLimit | `charge_current_limit` | Calcul CVL (`stats.ccl_limit_a`) avec repli reg. 103 | ×10 (0,1 A) sur 16 bits non signés.【F:data/tiny_read_4vic.json†L61-L92】【F:docs/victron_register_mapping.md†L44-L52】 |
| 0x351 / DischargeCurrentLimit | `discharge_current_limit` | Calcul CVL (`stats.dcl_limit_a`) avec repli reg. 102 | ×10 (0,1 A) sur 16 bits non signés.【F:data/tiny_read_4vic.json†L61-L92】【F:docs/victron_register_mapping.md†L44-L52】 |
| 0x35A / Alarm bits (2 bits) | `alarm_*` (par exemple `alarm_overvoltage`) | Fonctions `alarm_*` / `warn_*` alimentées par les seuils Victron & mesures TinyBMS | Valeur booléenne codée sur 2 bits (0 = OK, 1 = warning, 2 = alarm) publiée uniquement lors d’un changement d’état.【F:data/tiny_read_4vic.json†L95-L178】【F:legacy/arduino_src/bridge_uart.cpp†L435-L492】 |

Les suffixes affichés ci-dessus proviennent de la normalisation effectuée par `buildRegisterValue` : la fonction réutilise d’abord les clefs symboliques (`tiny_rw_mapping`) puis, à défaut, les libellés lisibles pour produire des segments compatibles MQTT (ex. « Battery Pack Voltage » → `battery_pack_voltage`).【F:legacy/arduino_src/mqtt/register_value.cpp†L40-L132】 L’ajout du `root_topic` nettoyé par `VictronMqttBridge::buildTopic` garantit des topics homogènes quel que soit l’input utilisateur.【F:legacy/arduino_src/mqtt/victron_mqtt_bridge.cpp†L397-L410】

## Cadence de publication et événements

1. **Télémétrie périodique** – Chaque cycle de sondage UART produit un snapshot `LiveDataUpdate` puis rejoue les enregistrements MQTT différés. La boucle publie d’abord les données agrégées (`LiveDataUpdate`) puis itère sur `deferred_mqtt_events` afin de pousser un `MqttRegisterValue` par registre vers le bus d’événements ; ces messages seront sérialisés et envoyés vers le broker par `VictronMqttBridge` à la fréquence `uart_poll_interval_ms_` (typiquement 20–200 ms selon l’adaptation).【F:legacy/arduino_src/bridge_uart.cpp†L365-L412】【F:legacy/arduino_src/mqtt/victron_mqtt_bridge.cpp†L129-L444】
2. **Alarmes et avertissements** – Les alarmes sont déclenchées uniquement lorsque les seuils configurés sont franchis ou qu’une condition exceptionnelle survient (ex. perte UART). La boucle UART compare les mesures courantes aux seuils Victron (`config.victron.thresholds`) et publie des événements `AlarmRaised`/`WarningRaised` via `publishAlarmEvent`. Ils ne sont donc émis qu’au moment d’un changement d’état, ce qui évite de spammer le broker tout en alignant les topics d’alarmes (`alarm_*`) sur la logique événementielle Victron.【F:legacy/arduino_src/bridge_uart.cpp†L414-L500】

`VictronMqttBridge` applique ensuite la politique QoS/retain déclarée dans `BrokerSettings`, avec possibilité d’overrides par publication pour distinguer télémétrie et alarmes selon les besoins du backend.【F:legacy/arduino_src/mqtt/victron_mqtt_bridge.cpp†L300-L366】

## Conversions et intervalles compatibles Victron

* **Unités CAN ↔ MQTT** – Les conversions reprises dans la table ci-dessus proviennent des définitions JSON `victron_can_mappings` (gains 100/10/1, arrondis, bornes) et garantissent l’alignement sur les résolutions attendues par les PGN Victron (0,01 V, 0,1 A, 0,1 %).【F:data/tiny_read_4vic.json†L5-L92】 Les mêmes facteurs sont réutilisés côté bridge CAN (`buildPGN_0x355/0x356/0x351`), assurant une cohérence CAN ↔ MQTT.【F:docs/victron_register_mapping.md†L25-L52】
* **Intervalle UART adaptatif** – Lors de l’initialisation, le bridge borne la période de sondage TinyBMS (`uart_poll_interval_ms_`) entre les limites configurables (min 20 ms, max configuré via `poll_interval_max_ms`) et applique les contraintes Victron sur les mises à jour PGN/keep-alive : PGN ≥ 100 ms, CVL ≥ 500 ms, keep-alive ≥ 200 ms. Ces garde-fous évitent de dépasser les cadences supportées par les GX tout en restant assez rapides pour la télémétrie MQTT.【F:legacy/arduino_src/bridge_core.cpp†L120-L148】
* **Timestamps et clamp** – Chaque `RegisterValue` transporte le timestamp de capture et conserve la valeur brute/échelle afin que les consommateurs puissent vérifier la conformité avant re-publication éventuelle. Les helper `clampQos` et la génération de topics évitent les débordements de QoS (>2) et les topics invalides.【F:legacy/arduino_src/mqtt/register_value.cpp†L92-L132】【F:legacy/arduino_src/mqtt/victron_mqtt_bridge.cpp†L144-L170】【F:legacy/arduino_src/mqtt/victron_mqtt_bridge.cpp†L330-L366】

### Identifiants recommandés

* Utiliser un `client_id` stable par passerelle afin de tirer parti du mécanisme de session persistante.
* Segmenter les topics par passerelle (`home/garage/battery/...`) si plusieurs TinyBMS sont supervisés.

### Bonnes pratiques

* Initialiser `mqtt::Publisher` au démarrage (`configure` + `connect`) avant de lancer les tâches UART/CAN.
* Appeler périodiquement `loop()` dans une tâche dédiée pour traiter les ré-essais de publication.
* Filtrer ou compresser la télémétrie (ex. ne publier que lorsque la variation est significative) pour limiter la charge réseau.

