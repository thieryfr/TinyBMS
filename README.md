# TinyBMS-Victron Bridge

## Description
Pont embarqué permettant de convertir les trames TinyBMS (UART/Modbus) vers le protocole CAN-BMS Victron. L'application s'appuie sur FreeRTOS, un Event Bus centralisé et une interface web/REST pour la supervision.

## Structure du Projet
```
TinyBMS/
├── README.md                       # Présent document
├── README_system_init.md           # Module initialisation
├── README_config_manager.md        # Module configuration JSON
├── README_event_bus.md             # Bus d'événements
├── README_uart.md                  # Acquisition TinyBMS UART
├── README_cvl.md                   # Algorithme CVL
├── README_watchdog.md              # Gestion watchdog
├── README_logger.md                # Journalisation
├── docs/
│   └── REVUE_MODULES.md            # Rapport de cohérence détaillé
├── data/                           # Contenu SPIFFS (config + UI)
├── include/                        # Headers partagés
├── src/                            # Implémentations C++/INO
├── tests/                          # Tests natifs & intégration
├── scripts/                        # Outils de déploiement
├── platformio.ini                  # Configuration PlatformIO
└── partitions.csv                  # Partitionnement ESP32
```

## Modules
- **Initialisation système** – Création des mutex, montage SPIFFS, WiFi, lancement des tâches FreeRTOS. Voir `README_system_init.md`.
- **Gestion de configuration** – Lecture/écriture JSON SPIFFS, notifications Event Bus. Voir `README_config_manager.md`.
- **Event Bus** – Publish/subscribe, cache d'événements, statistiques. Voir `README_event_bus.md`.
- **Acquisition UART TinyBMS** – Modbus RTU, publication des données live. Voir `README_uart.md`.
- **Algorithme CVL** – Calcul dynamique des limites CVL/CCL/DCL. Voir `README_cvl.md`.
- **Watchdog Manager** – Supervision Task WDT, statistiques feed. Voir `README_watchdog.md`.
- **Logger** – Journalisation SPIFFS + Serial avec rotation. Voir `README_logger.md`.
- **Rapport de revue** – Statuts, tests et actions correctives par module dans `docs/REVUE_MODULES.md`.

## Installation
1. Installer [PlatformIO Core](https://platformio.org/) ou l'extension VS Code correspondante.
2. Cloner le dépôt et ouvrir le dossier `TinyBMS/`.
3. Installer les dépendances PlatformIO (automatique lors de la première compilation) :
   - `ArduinoJson@^6.21.0`
   - `ESPAsyncWebServer@^1.2.3`
   - `AsyncTCP@^1.1.1`
   - `CAN@^0.3.1`

## Utilisation
1. Personnaliser `data/config.json` (WiFi, TinyBMS, Victron, CVL, logging).
2. Compiler et flasher :
   ```bash
   pio run
   pio run --target upload
   pio run --target uploadfs
   ```
3. Alimenter l'ESP32 et connecter TinyBMS (UART GPIO16/17) et Victron (CAN GPIO4/5).
4. Accéder à l'interface : `http://tinybms-bridge.local` (ou IP). WebSocket disponible sur `/ws`.

## Tests
- Tests d'intégration hors matériel :
  ```bash
  python -m pytest tests/integration/test_end_to_end_flow.py
  ```
- Tests natifs CVL :
  ```bash
  g++ -std=c++17 -Iinclude tests/test_cvl_logic.cpp src/cvl_logic.cpp -o /tmp/test_cvl
  /tmp/test_cvl
  ```
- Les résultats et scénarios de tests à blanc sont centralisés dans `docs/REVUE_MODULES.md`.

## Plan d'actions prioritaire
1. **Sécuriser les accès configuration** : généraliser `configMutex` dans les modules CAN/Web.
2. **Résoudre la double définition `webServerTask`** et ajouter des tests WebSocket.
3. **Automatiser la compilation ESP32** dans la CI (PlatformIO) et enrichir les tests natifs (Event Bus, CVL).
4. **Documenter les procédures de diagnostic** (watchdog, logs) et mutualiser le montage SPIFFS.
