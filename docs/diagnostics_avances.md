# Guide de diagnostics avancés

Ce guide regroupe les informations de supervision exposées par l'API `/api/status` et les autres points d'accès du firmware TinyBMS-Victron. Il complète les notes des modules (`README_watchdog.md`, `README_uart.md`, `docs/REVUE_MODULES.md`) pour faciliter les analyses terrain.

## 1. Interprétation des compteurs critiques

### 1.1 Watchdog matériel (Task WDT)

Les compteurs proviennent de `WatchdogManager` et sont accessibles via `watchdog.*` dans la réponse JSON.

| Champ | Signification | Seuils / actions recommandées |
| --- | --- | --- |
| `watchdog.enabled` | Indique si le Task WDT ESP32 est attaché à la tâche principale. | Doit être `true` en exploitation. Si `false`, relancer `Watchdog.enable()` via `/api/watchdog` après avoir vérifié l'initialisation SPIFFS/config. |
| `watchdog.timeout_ms` | Timeout configuré (dérivé de `config.advanced.watchdog_timeout_s`). | Valeur typique : 30000 ms (`data/config.json`). Adapter via `/api/watchdog` si les tâches prennent plus de 30 s, mais préférer optimiser les tâches bloquantes. |
| `watchdog.time_since_last_feed_ms` | Intervalle depuis le dernier `feed()`. | Doit rester nettement inférieur au timeout. Un warning est journalisé quand l'intervalle dépasse 90 % du timeout ; au-delà, inspecter les tâches UART/CAN/CVL qui alimentent le watchdog. |
| `watchdog.time_until_timeout_ms` | Temps restant avant reset. | Si proche de 0, forcer un `feed()` manuel (`PUT /api/watchdog`) et analyser les verrous (`feedMutex`, `uartMutex`). |
| `watchdog.feed_count` | Nombre total de `feed()` acceptés depuis l'initialisation. | Permet de vérifier que les tâches actives alimentent bien le WDT. Une stagnation du compteur indique une tâche bloquée. |
| `watchdog.health_ok` | Résultat de `checkHealth()` : `true` si `time_since_last_feed < timeout`. | Si `false`, un reset imminent est attendu ; sauvegarder les logs puis redémarrer la carte pour éviter un état incohérent. |
| `watchdog.last_reset_reason` | Raison du dernier reset (`ESP_RST_*`). | `TASK_WDT` ou `WDT` implique un dépassement watchdog ; corriger les boucles longues / mutex. `BROWNOUT` nécessite de vérifier l'alimentation. |

### 1.2 Statistiques CAN Victron

Les compteurs sont maintenus par `TinyBMS_Victron_Bridge::sendVictronPGN` et exposés dans `stats.can_*` (agrégés) et `stats.can.*` (détail driver).

| Champ | Signification | Seuils / actions recommandées |
| --- | --- | --- |
| `stats.can_tx_count` / `stats.can.tx_success` | Trames PGN émises avec succès depuis le démarrage. | Une progression continue confirme l'émission périodique (PGN 0x351/0x355/0x356/0x35A/0x35E/0x35F). Si le compteur stagne, vérifier `keepalive` et la tâche CAN. |
| `stats.can_rx_count` / `stats.can.rx_success` | Trames reçues (keepalive Victron). | Doit augmenter au rythme du keepalive (toutes les 1 s par défaut). Une absence de progression combinée à `keepalive.ok=false` indique une coupure CAN. |
| `stats.can_tx_errors` / `stats.can.tx_errors` | Erreurs d'émission CAN (driver). | Toute incrémentation doit générer une alarme (`ALARM_CAN_TX_ERROR`). Inspecter le câblage (terminaison 120 Ω) ou réduire le débit (`hardware.can.bitrate`). |
| `stats.can_rx_errors` | Erreurs de réception (frames corrompues). | Vérifier le blindage du câble et les masses. Peut être lié à du bruit électromagnétique. |
| `stats.can_bus_off_count` | Nombre d'états BUS-OFF remontés par le driver. | ≥ 1 implique une désactivation automatique du contrôleur : redémarrer le nœud et vérifier la terminaison CAN. |
| `stats.can_queue_overflows` / `stats.can.rx_dropped` | Trames abandonnées faute de place dans la file. | Survient si le traitement RX est trop lent. Augmenter la pile de la tâche CAN (`config.advanced.stack_size_bytes`) ou réduire la verbosité des logs CAN. |
| `stats.victron_keepalive_ok` | Booléen mis à jour par `BridgeKeepAlive`. | Si `false`, consulter `stats.keepalive.*` pour voir le dernier RX (`since_last_rx_ms`). Au-delà de `timeout_ms` (10 s par défaut), une alarme `KEEPALIVE_LOST` est levée. |

### 1.3 Statistiques UART TinyBMS

La tâche UART incrémente les compteurs dans `TinyBMS_Victron_Bridge::handleUartError` et `readTinyRegisters()`.

| Champ | Signification | Seuils / actions recommandées |
| --- | --- | --- |
| `stats.uart_success_count` / `stats.uart.success` | Nombre de lectures Modbus réussies. | Doit croître à chaque poll (toutes les 100 ms par défaut). Si le compteur cesse d'augmenter, contrôler l'alimentation du TinyBMS et les connexions RX/TX. |
| `stats.uart_errors` / `stats.uart.errors` | Erreurs logiques (exceptions Modbus). | Vérifier la configuration (`tinybms.uart_retry_count`) et les niveaux de tension. Peut signaler une incompatibilité de registre. |
| `stats.uart_timeouts` / `stats.uart.timeouts` | Absences de réponse avant `timeout_ms`. | >0 suggère du bruit ou un câble trop long. Ajuster `hardware.uart.timeout_ms` ou réduire `poll_interval_ms`. |
| `stats.uart_crc_errors` | Paquets reçus avec CRC invalide. | Corréler avec la qualité du câble et la masse. Si récurrent, activer `logging.log_uart_traffic` pour capturer les trames brutes. |
| `stats.uart_retry_count` | Nombre total de retries effectués. | Une augmentation rapide indique un bruit chronique. Vérifier l'intégrité de la liaison et la vitesse (`hardware.uart.baudrate`). |
| `stats.comm_lost` (JSON Live Data) | Flag booléen signalant la perte de communication. | Si `true`, l'UI déclenche une alarme `EVENT_WARNING_RAISED`. Reprendre les étapes ci-dessus avant de redémarrer le module. |

## 2. Procédure d'extraction des logs SPIFFS

Les journaux persistants sont stockés dans `/logs.txt` sur SPIFFS et peuvent être récupérés via l'API ou par PlatformIO.

### 2.1 Téléchargement via HTTP API

1. Authentifiez-vous si nécessaire (selon `config.web_server.enable_auth`).
2. Récupérez les logs au format texte encapsulé JSON :
   ```bash
   curl http://tinybms-bridge.local/api/logs/download | jq -r '.logs'
   ```
3. Pour supprimer le fichier après analyse :
   ```bash
   curl -X POST http://tinybms-bridge.local/api/logs/clear
   ```
4. Pour surveiller les logs en direct, activer les flags `log_can_traffic` / `log_uart_traffic` via `/api/config/logging` avant de télécharger.

### 2.2 Extraction via connexion série (fallback)

Si le réseau n'est pas disponible :

1. Connectez l'ESP32 en USB et ouvrez un terminal PlatformIO :
   ```bash
   pio device monitor --baud 115200
   ```
2. Redémarrez la carte pour rejouer les logs récents sur la sortie série.
3. En parallèle, il reste possible de monter SPIFFS et de copier `/logs.txt` via l'outil `esptool.py` :
   ```bash
   pio run --target uploadfs      # met à jour l'image SPIFFS locale
   esptool.py --chip esp32 read_flash 0x200000 0x100000 spiffs_dump.bin
   ```
4. Montez l'image localement avec `mkspiffs` ou `spiffs.py` pour extraire `logs.txt` si nécessaire.

## 3. Références rapides (seuils et configuration)

| Paramètre | Valeur par défaut | Impact |
| --- | --- | --- |
| `advanced.watchdog_timeout_s` | 30 s | Timeout utilisé pour `watchdog.timeout_ms`. |
| `victron.keepalive_interval_ms` | 1000 ms | Intervalle attendu entre deux RX `keepalive.last_rx_ms`. |
| `victron.keepalive_timeout_ms` | 10000 ms | Au-delà, `keepalive.ok=false` et alarme `KEEPALIVE_LOST`. |
| `tinybms.poll_interval_ms` | 100 ms | Fréquence de mise à jour des compteurs UART. |
| `tinybms.uart_retry_count` | 3 | Nombre maximum de retries avant erreur. |
| `hardware.uart.timeout_ms` | 1000 ms | Temps d'attente d'une réponse Modbus. |

Pour une analyse détaillée des modules, se référer à :

- `README_watchdog.md` pour les bonnes pratiques Task WDT.
- `README_uart.md` pour la gestion des erreurs Modbus et les tests hors matériel.
- `docs/REVUE_MODULES.md` pour le statut de chaque brique et les scénarios de test.

---

**Bonnes pratiques terrain**

1. Capturer un snapshot `/api/status` avant toute intervention pour comparer les compteurs avant/après.
2. Télécharger les logs SPIFFS puis les purger pour surveiller les événements récents après correctif.
3. Après un reset WDT ou une perte CAN, vérifier successivement : alimentation, câblage, paramètres `config.json`, état des mutex (`feedMutex`, `uartMutex`, `configMutex`).
