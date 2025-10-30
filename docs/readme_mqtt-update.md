# Mise à jour MQTT – Guide de validation

Ce document regroupe les informations nécessaires pour préparer et valider la
nouvelle passerelle MQTT TinyBMS ↔ Victron. Il couvre les prérequis, la
configuration logiciel/firmware, le flux de données attendu ainsi que les
procédures de test (automatisées et matérielles).

## Prérequis

### Matériel

- Une carte ESP32 flashée avec la version la plus récente du firmware TinyBMS
  (branch `main`).
- Un module TinyBMS réel connecté sur l’UART de l’ESP32.
- Un bus CAN Victron opérationnel (ou simulateur) pour surveiller la cohérence
  CAN ↔ MQTT.
- Une alimentation stabilisée pour TinyBMS + ESP32.
- Un environnement Victron sous **Venus OS** (GX, Cerbo, Raspberry Pi + Venus)
  avec accès réseau et un broker MQTT actif (Mosquitto ou broker intégré).

### Logiciel

- ESP-IDF 5.x et l’outil `idf.py` configurés pour la cible `esp32`.
- Python 3.10+ avec `pytest` pour exécuter les tests d’intégration (`pip
  install -r requirements.txt` si disponible).
- Un client MQTT pour vérification (ex. `mosquitto_sub`, MQTT Explorer, ou
  `mqtt-cli`).
- Accès aux fichiers de configuration Victron (`/data/conf/` sur Venus OS) afin
  de vérifier les topics publiés dans VRM.

## Configuration

1. **Compilation / flash**
   ```bash
   idf.py set-target esp32
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
2. **Paramètres Kconfig pertinents** (`idf.py menuconfig → TinyBMS bridge`)
   - `TINYBMS_MQTT_ENABLED` : activer le module MQTT.
   - `TINYBMS_MQTT_BROKER` / `PORT` : adresse du broker accessible par Venus OS.
   - `TINYBMS_MQTT_ROOT_TOPIC` : préfixe racine (ex. `tinybms/victron`).
   - `TINYBMS_MQTT_TELEMETRY_TOPIC` & `STATUS_TOPIC` : suffixes télémétrie et
     statut (peuvent rester vides, le firmware appliquera les valeurs
     `root/telemetry` et `root/status`).
3. **Venus OS / Broker**
   - S’assurer que `mosquitto` est activé (`Settings → Services → MQTT → ON`).
   - Récupérer l’adresse IP du GX et les identifiants MQTT si activé.
   - Sur un poste de test :
     ```bash
     mosquitto_sub -h <ip-gx> -t 'tinybms/#' -v
     ```
4. **Réseau**
   - Vérifier que l’ESP32 et l’équipement Victron sont sur le même VLAN.
   - Tester la résolution DNS si un FQDN est utilisé pour le broker.

## Flux de données

1. **Acquisition TinyBMS** : la tâche UART parse les lignes `V=…;I=…;SOC=…` et
   produit des `MeasurementSample`.
2. **Conversion MQTT** :
   - `mqtt::build_payload` applique les conversions Victron (V×100, A×10,
     température×10, SOC clampé 0–100 %).
   - `mqtt::payload_to_json` sérialise en JSON structuré :
     ```json
     {
       "timestamp_ms": 1234,
       "sequence": 7,
       "voltage_v": 52.100,
       "voltage_decivolt": 5210,
       "current_a": -23.450,
       "current_deciamp": -235,
       "soc_percent": 87.60,
       "soc_promille": 876,
       "temperature_c": 31.40,
       "temperature_decic": 314
     }
     ```
3. **Publication** : `mqtt::publish_sample` construit le topic
   `root/telemetry/live` (ou suffixe configuré) après normalisation (`TinyBMS
   Root → tinybms/root`).
4. **Consommateurs Victron** : Venus OS lit les topics MQTT, vérifie la
   cohérence avec les PGN CAN (0x355/0x356/0x351) et remonte l’état dans VRM.

## Procédures de test

### 1. Tests automatisés

- **Unitaires (host)** :
  ```bash
  g++ -std=c++17 -Wall -Wextra -Iinclude -Itests/native/stubs \
      tests/native/test_mqtt_formatter.cpp main/mqtt_formatter.cpp -o /tmp/test_mqtt && \
      /tmp/test_mqtt
  ```
  Valide la normalisation des topics, les conversions numériques et le format
  JSON via un mock MQTT.
- **Intégration Python** :
  ```bash
  python -m pytest tests/integration/test_end_to_end_flow.py
  ```
  Confirme la présence de la section `mqtt` dans `/api/status` et la cohérence
  des métriques UART → CAN → Web.

### 2. Tests fonctionnels en laboratoire

1. **Sanity check MQTT**
   - Démarrer `mosquitto_sub -t 'tinybms/#' -v`.
   - Alimenter le TinyBMS, vérifier que les messages `tinybms/telemetry/live`
     apparaissent toutes les 200 ms (selon cadence UART).
   - Contrôler que les champs `voltage_decivolt`, `current_deciamp` et
     `temperature_decic` correspondent aux valeurs affichées dans l’interface
     Web (`/api/status`).
2. **Tests de limites**
   - Simuler SOC > 100 % (en envoyant une trame forcée) → vérifier que le JSON
     reste borné à `soc_promille = 1000`.
   - Provoquer une température négative (simulateur ou registre TinyBMS) →
     confirmer la conversion signée (`temperature_decic` négatif).
   - Débrancher le broker → s’assurer que l’ESP32 log l’erreur, puis se
     reconnecte automatiquement.

### 3. Protocole matériel TinyBMS + Victron (Venus OS)

1. **Préparation**
   - Connecter l’ESP32 au réseau local du GX (Ethernet ou Wi-Fi).
   - Configurer le broker MQTT du GX (`Settings → Services → MQTT → LAN`).
   - Paramétrer TinyBMS pour publier la télémétrie (cadence 5–10 Hz) et activer
     les alarmes (seuils batterie basse / température haute).
2. **Vérification des topics**
   - Sur Venus OS, lancer `mosquitto_sub -t 'tinybms/#' -v`.
   - Vérifier la présence des topics :
     - `tinybms/telemetry/live` (télémétrie instantanée).
     - `tinybms/status/connected` (heartbeat toutes les `keepalive_period_ms`).
     - `tinybms/status/alarm/<nom>` lors d’une alarme (ex. `alarm_low_voltage`).
   - Contrôler que les suffixes sont normalisés (`Battery Pack Voltage` →
     `battery_pack_voltage`).
3. **Alarmes TinyBMS → Victron**
   - Forcer une alarme basse tension sur TinyBMS (définir un seuil proche du
     voltage actuel et décharger légèrement).
   - Attendre la publication MQTT :
     ```
     tinybms/status/alarm/low_voltage {"active":true,"timestamp_ms":...}
     ```
   - Sur le GX, vérifier que l’alarme correspondante apparaît dans `Notifications`
     et dans VRM (section `Alarm log`).
   - Réinitialiser l’alarme → confirmer la publication `{"active":false}` et la
     disparition côté Victron.
4. **Cohérence CAN ↔ MQTT**
   - Capturer les trames VE.Can (`candump`) et comparer `voltage_decivolt` avec
     le PGN 0x356.
   - Les bornes doivent correspondre (erreur < 1 %).
5. **Rapport**
   - Consigner les résultats (topics reçus, horodatages, codes d’erreur) dans le
     journal de test.
   - Mentionner toute divergence et l’horodatage pour reproduction.

## Références

- `docs/README_mqtt.md` – architecture complète du module MQTT historique.
- `docs/mqtt_integration.md` – extension Event Bus et mapping Victron.
- `tests/native/test_mqtt_formatter.cpp` – cas de test couvrant normalisation et
  conversions.
- `main/mqtt_formatter.cpp` – implémentation des fonctions testées.

