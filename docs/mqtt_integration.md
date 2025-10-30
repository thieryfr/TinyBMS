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

#### Compatibilité et nouveaux topics

- La mise à jour 2025 conserve le root topic historique `victron/tinybms` ; aucune reconfiguration côté broker ou automatisme n'est nécessaire.
- Les topics existants listés dans [`tests/fixtures/mqtt_topics_snapshot.json`](../tests/fixtures/mqtt_topics_snapshot.json) restent inchangés (voir tableau "Topics preserved" dans [docs/testing/mqtt_topic_regression.md](testing/mqtt_topic_regression.md)).
- Deux topics additionnels sont désormais publiés :
  - `pack_power_w` → `/Dc/0/Power` (puissance calculée, affichable directement par Venus OS).
  - `system_state` → `/System/0/State` (miroir de l'état TinyBMS pour ESS).
- Les tests automatisés `python -m pytest tests/integration/test_mqtt_topic_regression.py -v` garantissent que seule cette extension additive est introduite.

### Identifiants recommandés

* Utiliser un `client_id` stable par passerelle afin de tirer parti du mécanisme de session persistante.
* Segmenter les topics par passerelle (`home/garage/battery/...`) si plusieurs TinyBMS sont supervisés.

### Bonnes pratiques

* Initialiser `mqtt::Publisher` au démarrage (`configure` + `connect`) avant de lancer les tâches UART/CAN.
* Appeler périodiquement `loop()` dans une tâche dédiée pour traiter les ré-essais de publication.
* Filtrer ou compresser la télémétrie (ex. ne publier que lorsque la variation est significative) pour limiter la charge réseau.

