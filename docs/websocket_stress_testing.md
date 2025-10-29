# Guide de Tests de Stress WebSocket

Ce guide détaille les procédures de test de stress pour l'interface WebSocket du TinyBMS-Victron Bridge, incluant les scénarios multi-clients, les conditions réseau dégradées, et les métriques de performance à surveiller.

## 1. Architecture WebSocket

### 1.1 Implémentation

Le système WebSocket repose sur `ESPAsyncWebServer` avec les composants suivants :

- **Handler d'événements** (`src/websocket_handlers.cpp:35-51`) : Gère les connexions, déconnexions et erreurs
- **Tâche FreeRTOS** (`websocketTask`) : Diffusion périodique des données via Event Bus
- **Sérialisation JSON** (`buildStatusJSON`) : Construit les payloads avec `StaticJsonDocument<1536>`
- **Notification clients** (`notifyClients`) : Diffusion broadcast via `ws.textAll(json)`

### 1.2 Paramètres de Configuration

Les paramètres suivants (accessibles via `/api/config/web_server`) affectent les performances :

| Paramètre | Valeur par défaut | Impact sur performances |
| --- | --- | --- |
| `websocket_update_interval_ms` | 500 ms | Fréquence de diffusion. Valeurs < 50 ms augmentent la charge CPU. |
| `max_clients` | 4 | Limite de connexions simultanées (protection mémoire). |
| `enable_websocket` | `true` | Active/désactive le serveur WebSocket. |
| `port` | 80 | Port HTTP/WebSocket (standard). |

**Configuration avancée :**
- `config.advanced.stack_size_bytes` (défaut : 8192) : Taille de pile allouée à `websocketTask`
- `config.logging.log_can_traffic` : Active les logs de chaque diffusion WebSocket (impact performances)

## 2. Scénarios de Test Multi-Clients

### 2.1 Test de Charge Progressive

**Objectif :** Valider le comportement avec montée en charge de 1 à 10 clients.

**Procédure :**

1. **Préparation** : Configurer `websocket_update_interval_ms=100` pour augmenter la charge
2. **Exécution :**
   ```bash
   # Terminal 1 : Connexion client 1
   websocat ws://tinybms-bridge.local/ws --text

   # Terminal 2 : Connexion client 2
   websocat ws://tinybms-bridge.local/ws --text

   # ... Répéter jusqu'à 10 clients
   ```
3. **Observation :** Chaque client doit recevoir les mêmes données à ~100ms d'intervalle
4. **Analyse des logs :**
   ```bash
   curl http://tinybms-bridge.local/api/logs/download | jq -r '.logs' | grep WebSocket
   ```

**Critères de succès :**
- ✅ Tous les clients reçoivent les données sans perte
- ✅ `websocketTask stack` (ligne 188 de `websocket_handlers.cpp`) reste > 512 bytes
- ✅ Latence de diffusion < `interval_ms` + 50ms
- ❌ **Échec si :** Clients déconnectés, stack overflow, ou reset watchdog

### 2.2 Test de Saturation Mémoire

**Objectif :** Identifier les limites mémoire avec clients simultanés.

**Procédure :**

1. **Préparation :** Réduire `max_clients` à 2 via `/api/config/web_server`
2. **Exécution :** Tenter de connecter 5 clients simultanément
3. **Observation attendue :**
   - Clients 1-2 : Connexion réussie (`LOG_INFO: WebSocket client #X connected`)
   - Clients 3-5 : Refus de connexion par le serveur AsyncTCP
4. **Vérification :** Pas de reboot ou d'erreur fatale

**Critères de succès :**
- ✅ Le système refuse les connexions excédentaires sans crash
- ✅ Clients existants continuent de recevoir les données
- ❌ **Échec si :** Reset ESP32, erreur `ESP_RST_PANIC`, ou perte de clients actifs

### 2.3 Test de Déconnexions Rapides

**Objectif :** Vérifier la robustesse face aux déconnexions brutales.

**Procédure :**

1. **Script automatisé :**
   ```bash
   #!/bin/bash
   for i in {1..50}; do
       timeout 2s websocat ws://tinybms-bridge.local/ws --text &
       sleep 0.5
   done
   wait
   ```
2. **Exécution :** 50 connexions de 2s avec overlap
3. **Analyse :** Vérifier les logs pour `WS_EVT_DISCONNECT` sans erreur `WS_EVT_ERROR`

**Critères de succès :**
- ✅ Pas d'accumulation de clients fantômes (vérifier `/api/status` → `stats.websocket_clients`)
- ✅ Mémoire libre (`heap_free`) stable après test (récupérer via `/api/diagnostics`)
- ❌ **Échec si :** Fuites mémoire, augmentation progressive de `heap_fragmentation`

## 3. Tests Réseau Dégradé

### 3.1 Latence Élevée

**Objectif :** Valider la tolérance aux réseaux à latence élevée (Wi-Fi instable).

**Simulation (Linux) :**
```bash
# Ajouter 200ms de latence sur l'interface réseau
sudo tc qdisc add dev eth0 root netem delay 200ms

# Tester la connexion WebSocket
websocat ws://tinybms-bridge.local/ws --text
```

**Observation attendue :**
- Les données continuent d'arriver à `interval_ms` + latence
- Le serveur ne timeout pas les clients
- Les paquets sont mis en file d'attente par AsyncTCP

**Critères de succès :**
- ✅ Connexion maintenue malgré latence > 500ms
- ✅ Données reçues dans l'ordre (timestamp croissant)
- ❌ **Échec si :** Déconnexion automatique ou paquets corrompus

### 3.2 Perte de Paquets

**Simulation (Linux) :**
```bash
# Simuler 10% de perte de paquets
sudo tc qdisc add dev eth0 root netem loss 10%

# Monitorer les paquets reçus
websocat ws://tinybms-bridge.local/ws --text | ts '%H:%M:%.S'
```

**Observation attendue :**
- Certaines mises à jour peuvent ne pas arriver
- Les clients reçoivent la mise à jour suivante sans interruption
- Le serveur ne détecte pas d'erreur (TCP réessaie automatiquement)

**Critères de succès :**
- ✅ Connexion maintenue avec perte < 30%
- ✅ Pas d'erreur `WS_EVT_ERROR` dans les logs
- ❌ **Échec si :** Déconnexion permanente ou reset watchdog

### 3.3 Bande Passante Limitée

**Simulation (Linux) :**
```bash
# Limiter à 100kbps
sudo tc qdisc add dev eth0 root tbf rate 100kbit burst 10kb latency 50ms
```

**Analyse :**
- Calculer la taille JSON moyenne : ~1.5 Ko (avec registres)
- Débit théorique requis : `1.5 Ko × (1000ms / interval_ms) / max_clients`
- Exemple : `interval=100ms, max_clients=4` → `60 Ko/s` requis

**Critères de succès :**
- ✅ Dégradation gracieuse : augmentation de latence mais pas de perte
- ✅ Augmenter `websocket_update_interval_ms` réduit la charge réseau
- ❌ **Échec si :** Perte de paquets JSON ou déconnexions en cascade

## 4. Modes de Défaillance et Récupération

### 4.1 Stack Overflow de `websocketTask`

**Symptômes :**
- Reset watchdog (`ESP_RST_TASK_WDT`) ou panic (`ESP_RST_PANIC`)
- Log : `***ERROR*** A stack overflow in task websocketTask has been detected`

**Causes :**
- Trop de clients simultanés avec `interval_ms` très faible
- Payload JSON dépassant 1536 bytes (limite `StaticJsonDocument`)
- Récursion dans `buildStatusJSON` (rare)

**Récupération :**
1. Augmenter `config.advanced.stack_size_bytes` à 12288 via `/api/config/advanced`
2. Réduire `max_clients` ou augmenter `interval_ms`
3. Activer `log_can_traffic` temporairement pour capturer la taille JSON réelle

### 4.2 Dépassement Watchdog

**Symptômes :**
- Reset ESP32 après 30s de blocage (défaut `watchdog_timeout_s=30`)
- Log : `Task watchdog got triggered`

**Causes :**
- Mutex `feedMutex` non libéré (lignes 181-184 de `websocket_handlers.cpp`)
- Blocage dans `ws.textAll()` si un client ne répond pas
- Boucle infinie dans `buildStatusJSON`

**Récupération :**
1. Vérifier `/api/watchdog` pour `time_until_timeout_ms`
2. Forcer un `feed()` manuel : `PUT /api/watchdog`
3. Inspecter les mutex : consulter `diagnostics_avances.md` section 1.1

### 4.3 Fuite Mémoire Clients

**Symptômes :**
- `heap_free` décroît progressivement sur plusieurs heures
- Impossible de connecter de nouveaux clients malgré `max_clients` non atteint

**Diagnostic :**
```bash
# Surveiller la mémoire libre toutes les 10s
watch -n 10 'curl -s http://tinybms-bridge.local/api/diagnostics | jq ".heap_free"'
```

**Récupération :**
1. Identifier les clients fantômes via `/api/diagnostics` → `connected_websockets`
2. Redémarrer le serveur WebSocket : `POST /api/restart_websocket` (à implémenter)
3. Fallback : Reset complet via `POST /api/restart`

## 5. Métriques de Performance

### 5.1 Compteurs à Surveiller

Les métriques suivantes sont disponibles via `/api/status` et `/api/diagnostics` :

| Métrique | Localisation API | Interprétation |
| --- | --- | --- |
| `heap_free` | `/api/diagnostics` | Mémoire libre. Doit rester > 20 Ko sous charge. |
| `heap_fragmentation` | `/api/diagnostics` | Fragmentation < 30% recommandé. |
| `websocket_clients` | `/api/diagnostics` | Nombre de clients actifs (doit égaler `max_clients` max). |
| `websocket_tx_bytes` | `/api/diagnostics` | Octets transmis depuis démarrage (détecter fuites). |
| `websocketTask stack` | Logs série (ligne 188) | High water mark pile. Alerte si < 512 bytes. |
| `watchdog.time_since_last_feed_ms` | `/api/status` → `watchdog` | Intervalle depuis dernier `feed()`. Alerte si > 27000ms (90% timeout). |

### 5.2 Tests de Charge Recommandés

| Test | Configuration | Durée | Critère de succès |
| --- | --- | --- | --- |
| Charge nominale | 2 clients, 500ms interval | 1h | Aucune erreur, mémoire stable |
| Charge maximale | 4 clients, 100ms interval | 30min | Stack > 512 bytes, pas de reset |
| Stress réseau | 4 clients, 10% perte paquets | 15min | Connexions maintenues, latence < 2s |
| Endurance | 2 clients, 1000ms interval | 24h | Pas de fuite mémoire (heap stable ±5%) |

### 5.3 Baseline de Référence

**Conditions idéales (mesuré sur ESP32-DevKitC, 240MHz, 512KB RAM) :**

- **1 client, 500ms interval :** CPU ~5%, heap_free ~280 Ko, latence 10ms
- **4 clients, 100ms interval :** CPU ~15%, heap_free ~260 Ko, latence 50ms
- **4 clients, 50ms interval :** CPU ~25%, heap_free ~250 Ko, latence 100ms

**Seuils d'alerte :**
- CPU > 40% : Risque de dégradation des tâches UART/CAN
- heap_free < 100 Ko : Risque de reboot imminent (`ESP_RST_ALLOC_FAILED`)
- Latence > `interval_ms × 3` : Saturation réseau ou CPU

## 6. Intégration avec Event Bus (Phase 3)

### 6.1 Ordre de Publication

Depuis Phase 3 (voir `src/bridge_uart.cpp:291-297`), l'ordre de publication Event Bus garantit la cohérence :

1. **Snapshot complet** (`EVENT_LIVE_DATA_UPDATE`) publié en premier
2. **Registres MQTT individuels** publiés ensuite

**Impact sur WebSocket :**
- Les clients reçoivent toujours un snapshot cohérent (tous les registres appartiennent au même poll UART)
- Évite les incohérences temporelles (ex. SOC=80% reçu avant voltage=52V du même poll)

**Test de validation :**
```bash
# Capturer 100 messages WebSocket et vérifier timestamps
websocat ws://tinybms-bridge.local/ws --text | jq '.uptime_ms' | head -n 100
# Les timestamps doivent être strictement croissants
```

### 6.2 Cas Limites Event Bus

**Scénario :** Event Bus plein (buffer circulaire dépassé)

**Symptômes :**
- Certains clients reçoivent des données obsolètes
- Log : `[EventBus] Cache overflow for event type X`

**Récupération :**
- Augmenter `EVENT_BUS_CACHE_SIZE` dans `include/event_bus.h` (défaut : 16)
- Réduire `max_clients` ou augmenter `interval_ms` pour réduire la consommation Event Bus

## 7. Checklist Pré-Production

Avant déploiement en environnement réel, valider :

- [ ] Test de charge nominale (2 clients, 1h) sans erreur
- [ ] Test de charge maximale (4 clients, 30min) sans reset watchdog
- [ ] Test de latence réseau (500ms ajoutés) : connexion maintenue
- [ ] Test de perte de paquets (10%) : pas de déconnexion
- [ ] Test d'endurance (24h) : heap stable (variation < 5%)
- [ ] Logs captés et analysés (`/api/logs/download`) sans erreur critique
- [ ] Documentation utilisateur mise à jour avec limites observées
- [ ] Configuration optimale documentée dans `data/config.json`

## 8. Outils de Test Recommandés

### 8.1 Clients WebSocket CLI

- **websocat** : `cargo install websocat` ou `sudo apt install websocat`
- **wscat** : `npm install -g wscat`

### 8.2 Scripts d'Automatisation

**Test multi-clients parallèles :**
```bash
#!/bin/bash
# test_websocket_clients.sh
NUM_CLIENTS=${1:-4}
DURATION=${2:-300}  # 5 minutes

for i in $(seq 1 $NUM_CLIENTS); do
    timeout ${DURATION}s websocat ws://tinybms-bridge.local/ws --text \
        > /tmp/ws_client_$i.log 2>&1 &
    echo "Started client $i (PID $!)"
done

echo "Waiting for $DURATION seconds..."
sleep $DURATION

echo "Test completed. Check /tmp/ws_client_*.log for results."
```

**Analyse de latence :**
```bash
#!/bin/bash
# measure_latency.sh
websocat ws://tinybms-bridge.local/ws --text | \
    jq -r '.uptime_ms' | \
    awk 'BEGIN {prev=0} {if(prev>0) print $1-prev; prev=$1}'
```

### 8.3 Surveillance Réseau

**Capture de trafic avec `tcpdump` :**
```bash
# Capturer le trafic WebSocket sur 30s
sudo tcpdump -i wlan0 -s 0 -w /tmp/ws_capture.pcap \
    host tinybms-bridge.local and port 80 &
sleep 30
sudo pkill tcpdump

# Analyser avec Wireshark
wireshark /tmp/ws_capture.pcap
```

## 9. Références

- `src/websocket_handlers.cpp` : Implémentation complète
- `docs/diagnostics_avances.md` : Surveillance des compteurs critiques
- `docs/README_event_bus.md` : Architecture Event Bus et cache
- `docs/README_watchdog.md` : Bonnes pratiques Task WDT
- ESPAsyncWebServer documentation : https://github.com/me-no-dev/ESPAsyncWebServer

---

**Historique des révisions :**
- 2025-10-29 : Version initiale (Phase 3 optimisations)
