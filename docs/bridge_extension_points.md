# Points d'extension TinyBmsBridge

Cette synthèse recense les zones du code `main/bridge.cpp`, `main/config.cpp` et `main/diagnostics.cpp` permettant d'étendre la passerelle TinyBMS ↔ Victron.

## Initialisation matérielle et configuration
- **Chargement des paramètres matériels** : `load_bridge_config()` récupère les ports UART/CAN, les broches et les temporisations à partir de `sdkconfig`, puis résout automatiquement la configuration TWAI (bitrate, filtres, alertes). Les projets dérivés peuvent enrichir `BridgeConfig` (filtres personnalisés, longueurs de files) avant de construire `TinyBmsBridge`.【F:main/config.cpp†L9-L53】【F:include/config.hpp†L10-L35】
- **Point d'injection diagnostics** : `TinyBmsBridge::init()` initialise la structure `BridgeHealth` via `diagnostics::init` et permet d'ajouter des compteurs ou hooks supplémentaires avant le démarrage des tâches.【F:main/bridge.cpp†L60-L121】【F:main/diagnostics.cpp†L20-L31】
- **Personnalisation LED/statut** : `configure_led` et `set_led` offrent un emplacement simple pour brancher des indicateurs supplémentaires (ex. double LED, télémétrie) avant/après le démarrage du CAN.【F:main/bridge.cpp†L26-L44】【F:main/bridge.cpp†L338-L361】

## Boucle UART et parsing
- **Stratégie de parsing** : `parse_sample_line` convertit la télémétrie TinyBMS (clés `V`, `I`, `SOC`, `T`) vers `MeasurementSample`. L'ajout de nouvelles grandeurs se fait ici (ex. tension cellule mini/maxi) avant publication dans la file d'échantillons.【F:main/bridge.cpp†L211-L253】【F:include/bridge.hpp†L12-L18】
- **Publication différée** : `publish_sample` envoie l'échantillon dans la queue FreeRTOS et incrémente les métriques diagnostics. On peut remplacer `xQueueSend` par une file plus riche (filtrage, debounce) ou augmenter `sample_queue_length` via la config pour bufferiser davantage de mesures.【F:main/bridge.cpp†L256-L265】【F:main/config.cpp†L38-L41】
- **Tâche UART** : `uart_task` se charge de la lecture byte à byte et déclenche le parsing sur chaque ligne complète. Cette tâche peut être enrichie (gestion CRC, commandes TinyBMS) sans impacter le flux CAN grâce au découplage par queue.【F:main/bridge.cpp†L267-L306】

## Publication CAN et keepalive
- **Mapping signal → trame** : `can_task` convertit `MeasurementSample` en trame VE.Can 0x355 (tension×100, courant×10, SOC entier, température×10). L'injection de nouveaux PGN (0x351, 0x356…) peut réutiliser ce schéma (lecture queue → conversion → `twai_transmit`).【F:main/bridge.cpp†L308-L342】
- **Keepalive paramétrable** : le même `can_task` publie 0x351 à l'intervalle `keepalive_period_ms`. Les intégrateurs peuvent exposer d'autres battements (PGN 0x305) ou surveiller le watchdog Victron à partir de ce bloc.【F:main/bridge.cpp†L344-L361】【F:main/config.cpp†L38-L52】
- **Gestion des erreurs** : les appels `twai_transmit` alimentent `diagnostics::note_can_error`/`note_can_publish`, ouvrant la voie à des stratégies de retry ou de dégradation en fonction du compteur `BridgeHealth`.【F:main/bridge.cpp†L334-L358】【F:main/diagnostics.cpp†L48-L60】

## Supervision et arrêt
- **Tâche diagnostic** : `diagnostic_task` déclenche `diagnostics::log_snapshot` avec une période configurable. On peut y brancher des exports MQTT/WebSocket ou des triggers d'alarme supplémentaires.【F:main/bridge.cpp†L367-L373】【F:main/diagnostics.cpp†L62-L73】
- **Cycle de vie** : `start()`/`stop()` gèrent la création/destruction des tâches et pilotes. C'est l'endroit approprié pour ajouter de nouveaux workers (MQTT, stockage flash) en respectant l'ordre de nettoyage déjà en place.【F:main/bridge.cpp†L123-L191】

## Synthèse
La passerelle expose un pipeline clair : configuration (`BridgeConfig`) → collecte UART → queue d'échantillons → publication CAN → diagnostics. Chaque étape fournit un crochet explicite (structures extensibles, tâches spécialisées, compteurs) pour intégrer des besoins additionnels sans casser l'isolation FreeRTOS/TWAI existante.
