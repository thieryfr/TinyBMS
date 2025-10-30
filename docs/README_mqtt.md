# Module Passerelle MQTT Victron

## Rôle
Relayer les événements TinyBMS (valeurs de registre, alarmes, avertissements) vers un broker MQTT en respectant la convention Victron. Le module `VictronMqttBridge` s'abonne à l'Event Bus, construit des `RegisterValue` enrichis à partir des mappings TinyBMS et publie des topics dérivés (état système, alarmes Victron, puissance pack, etc.).

## Fichiers couverts
- `src/mqtt/victron_mqtt_bridge.cpp`
- `src/mqtt/register_value.cpp`
- `include/mqtt/victron_mqtt_bridge.h`
- `include/mqtt/publisher.h`

## Flux de données
1. `VictronMqttBridge::begin()` enregistre des abonnements Event Bus pour `MqttRegisterValue`, `AlarmRaised`, `AlarmCleared`, `WarningRaised`.
2. `configure(const BrokerSettings&)` normalise les paramètres (`sanitizeRootTopic`, clamp QoS) et conserve les identifiants/credentials.
3. Lors de `handleRegisterEvent`, chaque `MqttRegisterValue` est converti en `RegisterValue` via `buildRegisterValue()` (métadonnées issues de `tiny_read_mapping`). Les topics dérivés (tension, courant, état système, puissance, alarmes Victron) sont publiés via `publishRegister()` / `publishDerived()`.
4. Les alarmes (`AlarmRaised`/`AlarmCleared`/`WarningRaised`) sont traduites en topics spécifiques (`alarm_low_voltage`, etc.) grâce aux métadonnées `victron_alarm_utils`.
5. `appendStatus(JsonObject)` expose l'état du bridge (enabled/configured/connected, compteurs de publication, dernier code d'erreur) dans `/api/status`.
6. `loop()` gère la reconnexion périodique (`shouldAttemptReconnect`) lorsque la passerelle est activée mais déconnectée.

## Gestion de la connexion (ESP-IDF)
- `connect()` construit `esp_mqtt_client_config_t`, enregistre `onMqttEvent`, démarre le client et met à jour `connecting_/connected_`.
- `onMqttEvent` traite les callbacks `MQTT_EVENT_CONNECTED/DISCONNECTED/ERROR` pour mettre à jour les compteurs et les journaux.
- Le code natif (tests PC) court-circuite les appels ESP-IDF mais conserve les compteurs/logs pour validation.

## Configuration
- Paramètres chargés via `ConfigManager::MqttConfig` (`config.mqtt.*`) : URI, port, client ID, credentials, root topic, QoS par défaut, mode retain, keepalive, TLS.
- `VictronMqttBridge::enable()` active/désactive la passerelle dynamiquement (utilisé au boot selon `config.mqtt.enabled`).

## Tests
- `python -m pytest tests/integration/test_end_to_end_flow.py` valide l'abonnement Event Bus simulé, la présence de la section `mqtt` dans `/api/status` et le comptage `publish_count`.
- Tests manuels :
  - Connecter un broker (`mosquitto -v`), définir `config.mqtt.enabled=true` et vérifier les topics `root_topic/#`.
  - Provoquer une alarme (ex. tension haute) pour observer le topic `alarm_high_voltage`.
  - Débrancher le réseau pour valider la logique de reconnexion (`loop()` + logs `[MQTT]`).

## Améliorations possibles
- Bufferiser les événements lorsque `connected_` est faux afin de republier après reconnexion.
- Supporter des certificats clients (TLS mutuel) via `BrokerSettings`.
- Ajouter des tests natifs ciblant `buildRegisterValue` avec plusieurs bindings (numériques et texte).
