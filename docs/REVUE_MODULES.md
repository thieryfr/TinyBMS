# Revue de Cohérence par Module

## Module : Initialisation Système & Infrastructure

Statut: Fonctionnel

Fichiers associés :
```
src/system_init.cpp
include/system_init.h
src/main.ino
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Simulation de démarrage sans erreurs | Config par défaut (tests/fixtures/status_snapshot.json) | Initialisation séquentielle SPIFFS → WiFi → Tâches | Trace cohérente analysée via snapshot | ✅ |

Interopérabilité :

· Modules connectés : ConfigManager, Logger, EventBus, TinyBMS_Victron_Bridge, WatchdogManager, Web Server
· Points d'intégration : Création des mutex partagés, lancement des tâches FreeRTOS, publication d'états via EventBus
· Problèmes d'interface : Aucun constaté, mais dépendance forte à la disponibilité des mutex au démarrage

Actions correctives nécessaires :

· Documenter la séquence d'initialisation dans README principal pour faciliter l'intégration d'autres tâches (Phase 1)
· Ajouter un test de compilation automatisé (pio run) dès que l'environnement ESP32 est disponible en CI

---

## Module : Gestion de Configuration (ConfigManager)

Statut: Fonctionnel

Fichiers associés :
```
include/config_manager.h
src/config_manager.cpp
data/config.json (référence)
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Lecture configuration SPIFFS | Fichier tests/fixtures/status_snapshot.json → sections config | Chargement valeurs WiFi/Victron/CVL | Valeurs reconstituées via parsing manuel | ✅ |
| Publication d'événement | Invocations simulées via EventBus.publishConfigChange | Événement EVENT_CONFIG_CHANGED mis en cache | Dernier événement accessible dans JSON builders | ✅ |

Interopérabilité :

· Modules connectés : Logger, EventBus, Bridge UART/CAN, Web/API
· Points d'intégration : Mutex configMutex, publication de changements, snapshot CVL
· Problèmes d'interface : Accès concurrents non protégés dans certains modules (voir actions correctives)

Actions correctives nécessaires :

· Renforcer l'usage systématique de configMutex côté modules CAN/Web pour éviter les lectures non atomiques (priorité haute)
· Ajouter un test unitaire dédié au parsing JSON lorsque l'environnement ArduinoJson sera mocké

---

## Module : Event Bus

Statut: Fonctionnel

Fichiers associés :
```
include/event_bus.h
include/event_types.h
include/event_bus_config.h
src/event_bus.cpp
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Lecture cache live data | Fixtures UART → EventBus (tests/integration/test_end_to_end_flow.py) | getLatestLiveData retourne dernier paquet | Websocket + JSON consomment le cache | ✅ |
| Statistiques file d'attente | Dataset tests/fixtures/e2e_session.jsonl | stats.total_events_published suit trace | Valeurs présentes dans JSON status | ✅ |

Interopérabilité :

· Modules connectés : UART Task, CAN Task, CVL Task, WebSocket, JSON/API, Watchdog
· Points d'intégration : Publication live_data, alarmes, statut système, cache pour accès thread-safe
· Problèmes d'interface : Aucun bloquant observé

Actions correctives nécessaires :

· Ajouter un test unitaire natif sur la mise en cache (environnement FreeRTOS simulé) à moyen terme
· Exposer une API de drain pour diagnostic (optionnel)

---

## Module : Acquisition UART TinyBMS

Statut: Fonctionnel

Fichiers associés :
```
include/bridge_uart.h
src/bridge_uart.cpp
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Requêtes Modbus avec retry | Paramètres config.tinybms par défaut | Retries honorent configuration | Boucle d'essai/CRC vérifiée dans revue de code | ✅ |
| Publication live_data | Échantillons tests/fixtures/e2e_session.jsonl | Événements EVENT_LIVE_DATA_UPDATE accessibles | Consommation confirmée par tests intégration | ✅ |

Interopérabilité :

· Modules connectés : ConfigManager (timings), EventBus, Watchdog
· Points d'intégration : Mutex UART & config, stats UART dans bridge
· Problèmes d'interface : Aucun

Actions correctives nécessaires :

· Prévoir un mock TinyBMS pour tests unitaires hors matériel (priorité moyenne)
· Tracer dans Logger les contenus critiques (codes erreur Modbus) pour diagnostic terrain

---

## Module : CAN Victron & KeepAlive

Statut: À corriger

Fichiers associés :
```
include/bridge_can.h
include/bridge_keepalive.h
include/bridge_pgn_defs.h
src/bridge_can.cpp
src/bridge_keepalive.cpp
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Monotonicité compteur CAN | tests/fixtures/e2e_session.jsonl | Compteurs croissants | Vérifié via test_end_to_end_flow.py | ✅ |
| Perte keep-alive | Absence d'événements keepalive dans fixture | Alarme EVENT_ALARM_RAISED pour ALARM_CAN_KEEPALIVE_LOST | Publication confirmée dans JSON status | ✅ |

Interopérabilité :

· Modules connectés : ConfigManager, EventBus, Watchdog, JSON/API
· Points d'intégration : Construction PGN avec config victron, publication keepalive
· Problèmes d'interface : Accès à config.victron sans mutex (risque race lors d'une sauvegarde), doublon de responsabilités keepalive entre tâches

Actions correctives nécessaires :

· Enrober toutes les lectures de config.* dans bridge_can.cpp avec configMutex (priorité haute)
· Factoriser la logique keepalive pour éviter duplication bridging/can_driver (priorité moyenne)

---

## Module : Algorithme CVL

Statut: Fonctionnel

Fichiers associés :
```
include/cvl_logic.h
include/cvl_types.h
src/cvl_logic.cpp
tests/test_cvl_logic.cpp
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Transitions d'état CVL | Jeux SOC/imbalance (tests/test_cvl_logic.cpp) | États BULK → FLOAT → HOLD corrects | Test natif `/tmp/test_cvl` passé | ✅ |
| Publication EventBus | Capture e2e_session.jsonl | EVENT_CVL_STATE_CHANGED généré | Présence confirmée dans JSON status | ✅ |

Interopérabilité :

· Modules connectés : EventBus, ConfigManager, Bridge stats
· Points d'intégration : Snapshot config via mutex, stats.cvl_* exploités par CAN & JSON
· Problèmes d'interface : Aucun bloquant

Actions correctives nécessaires :

· Étendre les tests pour couvrir mode désactivé + limites extrêmes (priorité moyenne)
· Documenter les profils SOC → tension dans README_CVL

---

## Module : Interface Web / API / JSON / WebSocket

Statut: À corriger

Fichiers associés :
```
src/web_server_setup.cpp
src/web_routes_api.cpp
src/web_routes_tinybms.cpp
src/websocket_handlers.cpp
src/json_builders.cpp
include/web_routes.h
include/websocket_handlers.h
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Validation structure JSON | tests/fixtures/status_snapshot.json | Champs stats/can/alarms présents | Test pytest `test_status_snapshot_contains_new_sections` OK | ✅ |
| Diffusion WebSocket | Cache EventBus + update interval | Payload cohérent pour UI | Analysé via code (manque test automatique) | ⚠️ |

Interopérabilité :

· Modules connectés : EventBus, Bridge, Watchdog, ConfigManager, Logger
· Points d'intégration : Routes REST, WebSocket `/ws`, JSON status, upload config
· Problèmes d'interface : Double définition `webServerTask` (main.ino vs web_server_setup.cpp) susceptible d'erreur de linkage, accès config.* non protégé dans buildPGN/serveStatic, absence de test WebSocket

Actions correctives nécessaires :

· Supprimer le doublon `webServerTask` en centralisant la tâche dans web_server_setup.cpp (priorité haute)
· Ajouter tests end-to-end WebSocket via fixture/simulation (priorité moyenne)
· Protéger lectures config.* par configMutex dans routes JSON (priorité moyenne)

---

## Module : Watchdog Manager

Statut: Fonctionnel

Fichiers associés :
```
include/watchdog_manager.h
src/watchdog_manager.cpp
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Calcul statistiques feed | Simulation via status_snapshot.json | Moyennes/min/max cohérents | Valeurs disponibles dans JSON status | ✅ |
| Timeout forcé | Non couvert (manque banc) | Alarme anticipée via EventBus | À simuler ultérieurement | ⚠️ |

Interopérabilité :

· Modules connectés : System init, UART/CAN/CVL tasks, JSON/API
· Points d'intégration : Mutex feedMutex, publication statut santé
· Problèmes d'interface : Aucun critique

Actions correctives nécessaires :

· Prévoir scénario de test manuel pour vérifier reset ESP32 (priorité moyenne)
· Documenter procédures de diagnostic dans README_watchdog

---

## Module : Logger

Statut: Fonctionnel

Fichiers associés :
```
include/logger.h
src/logger.cpp
```

Tests à blanc réalisés :

| Scénario | Données d'entrée | Résultat attendu | Résultat obtenu | Status |
| --- | --- | --- | --- | --- |
| Rotation fichiers | Simulation taille >100Ko (à faire) | Fichier logs.txt recréé | Non testé faute d'environnement | ⚠️ |
| Capture niveaux log | Config logging.log_level = INFO | Messages filtrés correctement | Vérification code + snapshot | ✅ |

Interopérabilité :

· Modules connectés : Tous (via logger global)
· Points d'intégration : Mutex interne + configMutex, SPIFFS pour stockage
· Problèmes d'interface : Dépendance à SPIFFS.begin(true) redondante avec ConfigManager

Actions correctives nécessaires :

· Mutualiser le montage SPIFFS pour éviter appels multiples (priorité basse)
· Ajouter test unitaire sur changement de niveau via API (priorité moyenne)

