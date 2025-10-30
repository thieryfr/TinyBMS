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
