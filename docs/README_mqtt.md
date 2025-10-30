# Module Passerelle MQTT Victron

## Rôle
Diffuser les changements de registres TinyBMS vers un broker MQTT selon la convention Victron (topics dérivés des métadonnées de registre) et exposer l'état de la passerelle dans `/api/status`. Le module s'abonne à l'Event Bus (`MqttRegisterValue`) et publie des payloads JSON structurés tout en gérant la connexion/reconnexion ESP-IDF.

## Fichiers couverts
- `src/mqtt/victron_mqtt_bridge.cpp`
- `src/mqtt/register_value.cpp`
- `include/mqtt/victron_mqtt_bridge.h`
- `include/mqtt/publisher.h`

## Flux de données
1. `VictronMqttBridge::begin()` s'abonne à l'`EventBusV2` pour recevoir les `MqttRegisterValue` (produits par `bridge_uart` lors de la lecture TinyBMS et par `bridge_can` pour les valeurs PGN MQTT).
2. À chaque événement :
   - `handleRegisterEvent()` résout le `TinyRegisterRuntimeBinding` (mapping `tiny_read_mapping`), recompose la valeur (numérique + texte) et appelle `mqtt::buildRegisterValue()`.
   - `buildRegisterValue()` enrichit la structure `RegisterValue` avec les métadonnées issues des mappings read/write (`tiny_read_mapping`, `tiny_rw_mapping`), détermine un suffixe de topic (`sanitizeTopicComponent`) et copie les mots bruts.
   - `publishRegister()` construit un payload JSON compact (`address`, `value`, `raw`, `label`, `unit`, `timestamp_ms`, `key`, `comment`) et publie sur `root_topic/suffix` avec le QoS et le retain configurés.
3. `appendStatus(JsonObject)` est invoqué par `json_builders` pour exposer l'état MQTT : activé, configuré, connecté, compteurs de publications/erreurs, dernier message, paramètres QoS/retain.

📘 **Guide de validation** : consultez [`docs/readme_mqtt-update.md`](readme_mqtt-update.md) pour les prérequis, la configuration détaillée et le protocole de test TinyBMS ↔ Victron (topics, alarmes, validation hardware).

## Gestion de la connexion
- `configure(const BrokerSettings&)` normalise le root topic (`sanitizeRootTopic`), force un QoS ≤ 2 et stocke les credentials.
- `connect()` construit `esp_mqtt_client_config_t`, enregistre le callback `onMqttEvent` et lance `esp_mqtt_client_start`. Sur build natif (tests), la connexion est simulée et `connected_` passe à true.
- `loop()` tente une reconnexion (`shouldAttemptReconnect`) en fonction de `reconnect_interval_ms` lorsque la passerelle est activée mais déconnectée.
- `onMqttEvent` met à jour les drapeaux `connected_` / `connecting_`, incrémente `failed_publish_count_` en cas d'erreur et consigne les codes via `noteError`.

## Configuration
Les paramètres sont fournis via `BrokerSettings` (typiquement depuis `config.json` → `config.logging.mqtt` si ajouté) :
- `uri`, `port`, `client_id`, `username`, `password`.
- `root_topic` (nettoyé pour produire `sanitized_root_topic_`).
- `keepalive_seconds`, `reconnect_interval_ms`, `default_qos`, `retain_by_default`.
- TLS optionnel (`use_tls`, `server_certificate`).
`enable(bool)` permet d'activer/désactiver dynamiquement la passerelle (désactivation ⇒ `disconnect()` immédiat).

## Synchronisation
- Aucun mutex dédié : le module se contente de lire les événements (thread-safe via EventBus) et de publier côté tâche MQTT (ESP-IDF). Les compteurs internes (`publish_count_`, `failed_publish_count_`, `last_error_code_`) sont atomiques via single-thread context (callbacks). Les getters (`appendStatus`) n'utilisent pas de verrou car l'accès est séquentiel depuis la tâche JSON.

## Tests recommandés
- Intégration : `python -m pytest tests/integration/test_end_to_end_flow.py` vérifie la présence de la section `mqtt` dans `/api/status` et la publication d'événements lors des scénarios e2e (les topics sont simulés côté test via stub Publisher).
- Tests manuels :
  - Configurer un broker local (`mosquitto -v`) et activer la passerelle via config (`root_topic` défini). Vérifier les messages JSON sur `root_topic/#`.
  - Provoquer une déconnexion réseau pour observer la reconnexion automatique (`shouldAttemptReconnect`).
  - Forcer des registres string (ex: Manufacturer) pour valider `has_text_value` et le champ `text` du payload.

## Améliorations possibles
- Exposer un endpoint `/api/mqtt` pour modifier `BrokerSettings` à chaud et déclencher `configure` + `connect`.
- Ajouter un buffer de publication différée lorsque la connexion n'est pas disponible (actuellement les événements sont perdus).
- Implémenter un mode « retain personnalisé » selon la classe de registre (`value_class`).
- Ajouter une option pour publier au format `retain=false` mais avec `last will` pour indiquer l'état de la passerelle.

## Intégration temps réel avec les tâches existantes

### 1. Choix d'une tâche MQTT dédiée
- **Charge actuelle** :
  - La tâche UART (↑ priorité 12) consomme l'essentiel du temps CPU lors de la reconstruction des échantillons `MeasurementSample` et pousse les données dans `sample_queue_`.
  - La tâche CAN (↑ priorité 13) est volontairement courte : elle dépile l'échantillon, sérialise 8 octets et envoie le frame (blocage maximum 50 ms) tout en envoyant le keepalive périodique.
  - La tâche diagnostic (↓ priorité 5) effectue un journal périodique faible.
- **Contraintes MQTT** : la pile ESP-IDF (`esp_mqtt_client`) s'appuie sur des callbacks pouvant bloquer (socket/TLS) et nécessite une boucle de reconnexion. Mélanger ces appels réseau dans la tâche CAN allongerait la section critique et introduirait un risque de famine CAN lors de pertes réseau.
- **Décision** : créer une **nouvelle tâche FreeRTOS `tinybms_mqtt`** (pile 6–8 Ko, priorité 8) dédiée au pilotage du module MQTT. Cette priorité reste inférieure au CAN (13) et à l'UART (12) pour préserver la latence critique, mais supérieure au diagnostic afin de garantir la vidange régulière des buffers MQTT.

### 2. Structures de données partagées
- Étendre `TinyBmsBridge` avec :
  - Une queue `mqtt_sample_queue_` (longueur configurable, défaut 16) recevant les `MeasurementSample` côté UART, parallèlement à la queue CAN existante pour éviter le partage destructif.
  - Un `StaticSemaphore_t` + `SemaphoreHandle_t mqtt_state_mutex_` protégeant l'état interne du module MQTT (`connected_`, `pending_config_`, compteurs) consulté par `/api/status`.
- Pipeline :
  1. `uart_task` pousse chaque échantillon dans **deux** queues (`sample_queue_` pour le CAN, `mqtt_sample_queue_` pour le MQTT). En cas de saturation MQTT, le comportement est non bloquant (`xQueueSend` avec timeout court et incrément `diagnostics::note_dropped_sample_mqtt`).
  2. `mqtt_task` effectue `xQueueReceive` avec un timeout (200 ms) pour agréger les échantillons, puis publie via `VictronMqttBridge::publishSample()` tout en respectant le QoS configuré.
  3. Un `EventGroupHandle_t mqtt_events_` (bits `CONNECTED`, `CONFIGURED`, `PUBLISH_ERROR`) permet de réveiller la tâche sur changement d'état (callbacks MQTT).

### 3. API interne du module MQTT
- `esp_err_t mqtt::init(const BrokerSettings &settings, QueueHandle_t sample_queue, EventGroupHandle_t events);`
  - Initialise `VictronMqttBridge`, stocke les handles de queue/événements et lance la machine d'état (sans créer de tâche).
- `void mqtt::start();`
  - Active le pont (`enable(true)`), déclenche la connexion initiale et enregistre les callbacks `on_connected`, `on_disconnected`, `on_error`.
- `void mqtt::stop();`
  - Force `enable(false)`, vide la queue et désenregistre les callbacks.
- `bool mqtt::publish_sample(const MeasurementSample &sample);`
  - Sérialise un `MeasurementSample` en `RegisterValue` via `buildRegisterValue()` et publie le JSON correspondant.
- `void mqtt::on_config_update(const BrokerSettings &settings);`
  - Appliqué lorsqu'un changement arrive via la configuration (fichier ou API). Met à jour `pending_config_`, notifie la tâche par `xEventGroupSetBits(events, CONFIG_UPDATED)`.
- Callbacks ESP-IDF :
  - `handle_mqtt_connected()` → `xEventGroupSetBits(events, CONNECTED_BIT)` et déverrouille `mqtt_state_mutex_` pour actualiser l'état.
  - `handle_mqtt_disconnected()` → bit `DISCONNECTED_BIT`, incrément compteur d'erreur.
  - `handle_mqtt_published()` → bit `PUBLISHED_BIT` pour réveiller la tâche si elle attend un accusé QoS1.

### Points d'intégration configuration
- `tinybms::load_bridge_config()` : ajoute un bloc `config.mqtt` (timeout connexion, profondeur queue, QoS) transmis à `mqtt::init`.
- `TinyBmsBridge::start()` : crée la queue MQTT, l'événement et la tâche `mqtt_task_entry` après la tâche CAN.
- `/api/status` : `appendStatus(JsonObject)` doit lire les compteurs via `mqtt_state_mutex_` pour garantir la cohérence.
- `diagnostics` : ajouter des compteurs `note_mqtt_publish()`, `note_mqtt_error()`, `note_mqtt_dropped_sample()` pour la supervision globale.
