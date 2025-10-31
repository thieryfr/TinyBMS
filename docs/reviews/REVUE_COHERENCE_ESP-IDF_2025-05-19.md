# Revue de Cohérence du Projet TinyBMS – Migration ESP-IDF (mai 2025)

## Module – HAL ESP-IDF (UART, CAN, Storage, Watchdog)
- **Objectif du module** : Proposer des pilotes ESP-IDF natifs (UART, CAN/TWAI, SPIFFS, watchdog) instanciés via le `HalManager` pour alimenter les couches applicatives.
- **Statut actuel** : Fonctionnel – chaque pilote vérifie sa configuration, évite les réinstallations inutiles et remet le périphérique à zéro en cas de changement.【F:src/hal/esp32_idf/esp32_uart_idf.cpp†L34-L185】【F:src/hal/esp32_idf/esp32_can_idf.cpp†L24-L222】【F:src/hal/esp32_idf/esp32_storage_idf.cpp†L90-L158】【F:src/hal/esp32_idf/esp32_hal_idf_others.cpp†L183-L288】
- **Vérification de la cohérence des flux avec données** : `HalManager::initialize` peut appeler plusieurs fois les pilotes sans erreur ; les chemins de réinitialisation gèrent la suppression/réinstallation des drivers et publient les journaux IDF pour tracer chaque étape.【F:src/hal/esp32_idf/esp32_uart_idf.cpp†L40-L105】【F:src/hal/esp32_idf/esp32_can_idf.cpp†L30-L76】
- **Interopérabilité** : Les instances sont retournées sous forme de `std::unique_ptr` et consommées par le bridge, le logger, le watchdog et les routes API via `HalManager` (non modifié).
- **Points à finaliser/améliorer** : Étendre le support des filtres CAN multiples – seul le premier filtre est appliqué actuellement (warning journalisé).【F:src/hal/esp32_idf/esp32_can_idf.cpp†L180-L202】
- **Problèmes identifiés et actions correctives** : Aucun dysfonctionnement bloquant relevé.

## Module – Initialisation Système & Gestion des Tâches
- **Objectif du module** : Orchestrer le montage SPIFFS, le chargement des mappings, l’initialisation réseau/MQTT/bridge et la création des tâches FreeRTOS.
- **Statut actuel** : Fonctionnel – la séquence documente chaque étape et publie l’état sur l’EventBus pour assurer la visibilité globale.【F:src/system_init.cpp†L360-L482】
- **Vérification de la cohérence des flux avec données** : Les dépendances SPIFFS→mappings→Wi-Fi→MQTT→bridge sont respectées, avec publication d’évènements système à chaque réussite/échec.【F:src/system_init.cpp†L367-L424】【F:src/system_init.cpp†L459-L477】
- **Interopérabilité** : Les tâches créées consomment `configMutex`, `feedMutex`, `eventBus` et les services HAL ; les échecs sont remontés via logs et EventBus.【F:src/system_init.cpp†L324-L455】
- **Points à finaliser/améliorer** : Documenter dans la configuration que l’échec SPIFFS bloque les mappings pour accélérer le diagnostic terrain (pas de code manquant, mais documentation).
- **Problèmes identifiés et actions correctives** : Aucun blocage identifié.

## Module – Acquisition UART (bridge_uart)
- **Objectif du module** : Lire les registres TinyBMS via Modbus, mettre à jour `TinyBMS_LiveData`, publier les alarmes et alimenter l’EventBus/MQTT.
- **Statut actuel** : Fonctionnel – la lecture verrouille `uartMutex`, adapte les timeouts depuis la configuration et alimente les statistiques bridge.【F:src/bridge_uart.cpp†L80-L183】
- **Vérification de la cohérence des flux avec données** : Les options de transaction sont recalculées à partir de la config (avec repli journalisé), la lecture remplit la fenêtre de sondage et met à jour les compteurs de succès/erreur.【F:src/bridge_uart.cpp†L119-L183】
- **Interopérabilité** : L’accès configuration/statistiques est protégé par mutex, les évènements sont publiés via `BridgeEventSink` (voir module EventBus), et le watchdog est nourri pendant les boucles de sondage.【F:src/bridge_uart.cpp†L83-L180】
- **Points à finaliser/améliorer** : Ajouter une trace métier explicite quand la configuration de repli est utilisée (log déjà présent mais peu contextualisé).
- **Problèmes identifiés et actions correctives** : Aucun bug bloquant détecté.

## Module – CAN PGN & KeepAlive
- **Objectif du module** : Mapper les LiveData vers les PGN Victron, gérer le keepalive VE.Can et publier les alarmes bus.
- **Statut actuel** : Fonctionnel – les conversions chargent les seuils Victron, mettent à jour les compteurs et publient sur l’EventBus.【F:src/bridge_can.cpp†L53-L220】
- **Vérification de la cohérence des flux avec données** : Les seuils sont mis en cache sous mutex, les trames PGN utilisent les mappers utilitaires et les alarmes sont annotées avant publication.【F:src/bridge_can.cpp†L164-L220】
- **Interopérabilité** : Le module dépend de `HalManager::can()`, de la configuration partagée et du watchdog pour nourrir le chien dans les boucles (hors extrait).
- **Points à finaliser/améliorer** : Ajouter une métrique exposant l’horodatage du dernier keepalive reçu pour la supervision distante.
- **Problèmes identifiés et actions correctives** : Aucun défaut fonctionnel constaté.

## Module – Logique CVL
- **Objectif du module** : Calculer dynamiquement les limites CVL/CCL/DCL et gérer les transitions d’état Victron.
- **Statut actuel** : Fonctionnel – la tâche CVL récupère le dernier LiveData, calcule les limites et publie un évènement `CVLStateChanged` lors des transitions.【F:src/bridge_cvl.cpp†L33-L187】
- **Vérification de la cohérence des flux avec données** : Les snapshots config sont rechargés sous mutex, les résultats sont enregistrés dans les stats, et chaque transition publie durée et nouvelles limites.【F:src/bridge_cvl.cpp†L33-L170】
- **Interopérabilité** : Lecture/écriture protégées par `configMutex`/`statsMutex`, watchdog nourri à chaque boucle et publication via `BridgeEventSink`.
- **Points à finaliser/améliorer** : Exposer les durées cumulées par état via l’API pour faciliter l’analyse (les durées sont déjà calculées).【F:src/bridge_cvl.cpp†L156-L166】
- **Problèmes identifiés et actions correctives** : Aucun défaut logique observé.

## Module – EventBus & BridgeEventSink
- **Objectif du module** : Fournir un bus d’évènements thread-safe avec cache du dernier message et abonnement/désabonnement RAII.
- **Statut actuel** : Fonctionnel – la publication renseigne automatiquement les métadonnées, alimente le cache et notifie chaque abonné.【F:include/event/event_bus_v2.tpp†L13-L125】
- **Vérification de la cohérence des flux avec données** : `publish` remplit timestamp/sequence, `subscribe` gère les abonnements sous mutex et `getLatest` renvoie le dernier message pour les consommateurs tardifs.【F:include/event/event_bus_v2.tpp†L13-L103】
- **Interopérabilité** : Tous les modules (UART, CAN, CVL, MQTT, WebSocket) s’appuient sur le même bus ; le cache `latest` alimente notamment le WebSocket.
- **Points à finaliser/améliorer** : Ajouter un suivi du nombre de subscribers par type si besoin de détection de fuites.
- **Problèmes identifiés et actions correctives** : Aucun.

## Module – Gestion Configuration
- **Objectif du module** : Charger/sauvegarder la configuration JSON depuis le stockage HAL et diffuser les changements sur l’EventBus.
- **Statut actuel** : Fonctionnel – chargement SPIFFS, verrouillage mutex, publication d’évènement `ConfigChanged` et persistance sont opérationnels.【F:src/config_manager.cpp†L90-L159】
- **Vérification de la cohérence des flux avec données** : Chaque section (Wi-Fi, matériel, Victron, MQTT, Web, logging…) est extraite séquentiellement puis sauvegardée dans le même ordre.【F:src/config_manager.cpp†L112-L159】
- **Interopérabilité** : Dépend du stockage HAL, publie un évènement global après chargement/sauvegarde, et fournit les paramètres aux autres modules via `configMutex`.
- **Points à finaliser/améliorer** : Ajouter une validation de schéma/valeurs limites avant publication pour éviter les configurations hors bornes.
- **Problèmes identifiés et actions correctives** : Aucun.

## Module – Logger & Persistance
- **Objectif du module** : Fournir une journalisation thread-safe (série + SPIFFS) avec rotation automatique et niveau configurable.
- **Statut actuel** : Fonctionnel – initialisation sous mutex, rotation à 100 Ko, lecture/effacement pilotés via HAL stockage.【F:src/logger.cpp†L36-L147】
- **Vérification de la cohérence des flux avec données** : Les entrées sont horodatées, écrites en série puis fichier sous verrou, et la rotation réouvre le fichier proprement.【F:src/logger.cpp†L79-L105】
- **Interopérabilité** : Consomme `configMutex` pour le niveau de log et `HalManager::storage()` pour le fichier, accessible aux routes Web/API.
- **Points à finaliser/améliorer** : Rendre le seuil de rotation configurable pour s’adapter à la taille SPIFFS.
- **Problèmes identifiés et actions correctives** : Aucun.

## Module – Web Server & Routes (variante ESP-IDF)
- **Objectif du module** : Fournir un wrapper `esp_http_server` compatible avec l’API Async (routes dynamiques, CORS, auth, fichiers statiques).
- **Statut actuel** : Fonctionnel – les routes sont empilées avant démarrage, `serveStatic` sert SPIFFS (gzip/fallback SPA) et CORS/auth sont configurables au niveau serveur.【F:src/web/esp_http_server_wrapper.cpp†L250-L501】
- **Vérification de la cohérence des flux avec données** : `web_server_setup` active l’auth/CORS selon la configuration et lance le serveur IDF avant d’enregistrer le WebSocket.【F:src/web_server_setup.cpp†L52-L128】
- **Interopérabilité** : Les routes API TinyBMS utilisent le même wrapper et partagent logger/config ; le WebSocket est lié après démarrage serveur.【F:src/web_server_setup.cpp†L87-L128】
- **Points à finaliser/améliorer** : Ajouter la prise en charge des méthodes HEAD si nécessaire et documenter l’activation TLS.
- **Problèmes identifiés et actions correctives** : Aucun blocage côté HTTP statique/API.

## Module – WebSocket Temps Réel
- **Objectif du module** : Diffuser LiveData/statuts JSON vers les clients WebSocket avec throttling configurable.
- **Statut actuel** : À corriger – les clients ESP-IDF ne sont ajoutés à `clients_` qu’après la réception d’un premier message texte, donc un client qui attend un push après handshake n’est pas diffusé.【F:include/esp_websocket_wrapper.h†L131-L209】
- **Vérification de la cohérence des flux avec données** : La tâche WebSocket lit la configuration, applique le throttling et publie via `ws.textAll`, mais dépend de l’inscription correcte des clients.【F:src/websocket_handlers.cpp†L94-L190】
- **Interopérabilité** : Consomme l’EventBus, protège les lectures config/stats par mutex et nourrit le watchdog dans la boucle (voir module WebSocket handlers).
- **Points à finaliser/améliorer** : Enregistrer les clients dès le handshake (`HTTP_GET`) et nettoyer les sockets lors des frames CLOSE pour garantir la diffusion immédiate.【F:include/esp_websocket_wrapper.h†L131-L223】
- **Problèmes identifiés et actions correctives** : Implémenter l’inscription lors de `HTTP_GET` (ajout dans `clients_`) et propager l’évènement Connect sans attendre une frame texte.

## Module – Passerelle MQTT Victron
- **Objectif du module** : Publier les registres/alarmes TinyBMS sur MQTT et gérer les reconnexions broker.
- **Statut actuel** : En développement – le backend ESP-IDF gère l’initialisation `esp_mqtt_client` et relaie les évènements, mais la remontée d’erreurs reste limitée (code d’évènement uniquement).【F:src/mqtt/esp_idf_mqtt_backend.cpp†L24-L145】
- **Vérification de la cohérence des flux avec données** : La bridge configure le broker, démarre le backend, publie les registres JSON et suit les compteurs de réussites/échecs.【F:src/mqtt/victron_mqtt_bridge.cpp†L201-L399】
- **Interopérabilité** : Les évènements EventBus (valeurs, alarmes, warnings) sont relayés vers MQTT ; les callbacks backend mettent à jour les indicateurs `connecting_`/`connected_` et les erreurs internes.【F:src/mqtt/victron_mqtt_bridge.cpp†L238-L386】
- **Points à finaliser/améliorer** : Étendre la journalisation des évènements MQTT pour distinguer erreurs TLS/transport/authentification et exposer le dernier `esp_err_t` côté statut.
- **Problèmes identifiés et actions correctives** : Ajouter le décodage détaillé de `mqtt_event_t` (esp_tls, return_code) afin de guider les scénarios de reconnexion.

## Module – Gestionnaire Watchdog
- **Objectif du module** : Configurer, activer et nourrir le watchdog matériel tout en collectant des statistiques d’alimentation.
- **Statut actuel** : Fonctionnel – configuration, enable/disable et feed sont encapsulés avec protections mutex, et les intervalles min/max/moyenne sont calculés.【F:src/watchdog_manager.cpp†L37-L171】
- **Vérification de la cohérence des flux avec données** : Les feeds tardifs génèrent un warning, les statistiques sont mises à jour à chaque reset et le watchdog HAL est sollicité via `HalManager` (voir HAL Watchdog).【F:src/watchdog_manager.cpp†L100-L171】【F:src/hal/esp32_idf/esp32_hal_idf_others.cpp†L197-L278】
- **Interopérabilité** : Partage `feedMutex`, consomme la configuration avancée pour le timeout et expose les stats aux autres modules.
- **Points à finaliser/améliorer** : Ajouter une télémétrie par tâche indiquant la dernière alimentation.
- **Problèmes identifiés et actions correctives** : Aucun dysfonctionnement.

## Module – Monitoring Système
- **Objectif du module** : Collecter et journaliser les statistiques de pile et de mémoire FreeRTOS pour détecter les dérives.
- **Statut actuel** : Fonctionnel – les fonctions exposent les stats des tâches principales, la mémoire heap et les métriques d’uptime.【F:src/system_monitor.cpp†L24-L175】
- **Vérification de la cohérence des flux avec données** : Les handles connus sont interrogés, les logs détaillent pile libre et fragmentation heap ; les warnings sont générés si seuils dépassés.【F:src/system_monitor.cpp†L52-L175】
- **Interopérabilité** : S’appuie sur les handles créés dans `system_init` et sur le logger pour publier les résultats.
- **Points à finaliser/améliorer** : Remplacer l’hypothèse de pile fixe (8192 octets) par la valeur réelle passée à `xTaskCreate` pour des pourcentages exacts.【F:src/system_monitor.cpp†L37-L45】
- **Problèmes identifiés et actions correctives** : Aucun blocage.

---

### Synthèse des actions prioritaires
1. **WebSocket ESP-IDF** : enregistrer les clients durant le handshake `HTTP_GET` et nettoyer `clients_` dès `HTTPD_WS_TYPE_CLOSE` pour garantir la diffusion immédiate des push serveur.【F:include/esp_websocket_wrapper.h†L131-L223】
2. **Backend MQTT ESP-IDF** : enrichir la journalisation des évènements (`mqtt_event_t`) afin de distinguer les causes de déconnexion/erreur et d’améliorer les scénarios de reconnexion automatisée.【F:src/mqtt/esp_idf_mqtt_backend.cpp†L114-L135】
