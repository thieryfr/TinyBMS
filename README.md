# TinyBMS-Victron Bridge

Un pont UART-CAN entre systèmes de gestion de batterie TinyBMS et systèmes énergétiques Victron, basé sur ESP32.

## 📋 Vue d'ensemble

Ce projet implémente un pont de communication permettant aux batteries gérées par TinyBMS de communiquer avec les systèmes Victron (GX, MultiPlus, etc.) via le protocole CAN-BMS de Victron.

### Fonctionnalités principales

- ✅ **Pont UART ↔ CAN** : Traduit les données TinyBMS (UART/Modbus) vers le protocole Victron CAN-BMS
- ✅ **Interface Web** : Dashboard responsive Bootstrap 5.3 pour monitoring en temps réel
- ✅ **Algorithme CVL** : Gestion intelligente de la tension de charge (Charge Voltage Limit) basée sur le SOC
- ✅ **FreeRTOS** : Architecture multi-tâches robuste avec watchdog
- ✅ **Logging** : Système de journalisation avec rotation des fichiers sur SPIFFS
- ✅ **WebSocket** : Transmission temps réel des données vers l'interface web
- ✅ **Configuration JSON** : Configuration complète via fichiers JSON sur SPIFFS

## 🔧 Matériel requis

- **ESP32** (ESP32-DevKitC ou équivalent)
- **Module CAN** (MCP2515/TJA1050 ou intégré)
- **TinyBMS** avec interface UART
- **Câbles** :
  - UART : RX (GPIO 16), TX (GPIO 17)
  - CAN : TX (GPIO 5), RX (GPIO 4)

### Schéma de connexion

```
TinyBMS UART ───> ESP32 (GPIO 16/17) ───> CAN Transceiver ───> Victron CAN-BMS
```

## 🚀 Installation

### Prérequis

- [PlatformIO](https://platformio.org/) ou Arduino IDE
- Python 3.x (pour le script de déploiement)

### Compilation

```bash
# Avec PlatformIO
pio run

# Upload du firmware
pio run --target upload

# Upload des fichiers SPIFFS (config.json, web interface)
pio run --target uploadfs
```

### Configuration

1. Éditez `data/config.json` avec vos paramètres :
   - WiFi SSID/password
   - Pins matériels (UART, CAN)
   - Paramètres CVL
   - Niveaux de log

2. Téléchargez le fichier sur SPIFFS avant le premier démarrage

## 📁 Structure du projet

```
TinyBMS/
├── main.ino                      # Point d'entrée principal
├── config.json                   # Configuration système
├── platformio.ini                # Configuration PlatformIO
│
├── Core/
│   ├── tinybms_victron_bridge.cpp/h  # Pont UART-CAN principal
│   ├── config_manager.cpp/h          # Gestion configuration JSON
│   ├── logger.cpp/h                  # Système de logging
│   └── watchdog_manager.cpp          # Watchdog matériel
│
├── FreeRTOS/
│   ├── rtos_tasks.h              # Déclarations tâches
│   ├── rtos_config.h             # Configuration FreeRTOS
│   └── shared_data.h             # Structures partagées
│
├── Web/
│   ├── web_server_setup.cpp      # Initialisation serveur web
│   ├── web_routes_api.cpp        # Routes API REST
│   ├── websocket_handlers.cpp/h  # Gestion WebSocket
│   ├── json_builders.cpp/h       # Construction JSON
│   └── [fichiers .html/.js/.css] # Interface web
│
└── System/
    └── system_init.cpp           # Initialisation WiFi/UART/CAN
```

## 🌐 Interface Web

Accédez à l'interface via `http://tinybms-bridge.local` ou l'adresse IP de l'ESP32.

### Endpoints API

- `GET /api/status` - État en temps réel (voltage, courant, SOC, etc.)
- `GET /api/config/system` - Configuration système
- `GET /api/config/tinybms` - Configuration TinyBMS
- `GET /api/logs` - Logs système
- `POST /api/config/save` - Sauvegarder configuration

### WebSocket

Connexion : `ws://tinybms-bridge.local/ws`

Les données sont diffusées toutes les 1000ms (configurable).

## ⚙️ Configuration

### WiFi

```json
{
  "wifi": {
    "ssid": "VotreSSID",
    "password": "VotreMotDePasse",
    "ap_fallback": {
      "enabled": true,
      "ssid": "TinyBMS-Bridge",
      "password": "12345678"
    }
  }
}
```

### Algorithme CVL

L'algorithme CVL ajuste dynamiquement la tension de charge basée sur le SOC :

- **BULK** (SOC < 90%) : Tension maximale
- **TRANSITION** (90-95%) : Réduction progressive
- **FLOAT_APPROACH** (95-100%) : Approche float
- **FLOAT** (100%) : Tension de maintien
- **IMBALANCE_HOLD** : Maintien si déséquilibre cellules

```json
{
  "cvl_algorithm": {
    "enabled": true,
    "bulk_soc_threshold": 90.0,
    "transition_soc_threshold": 95.0,
    "float_soc_threshold": 100.0,
    "float_offset_mv": 100
  }
}
```

## 🐛 Débogage

### Niveaux de log

- `ERROR` : Erreurs critiques uniquement
- `WARNING` : Avertissements + erreurs
- `INFO` : Informations générales (défaut)
- `DEBUG` : Tous les messages (verbeux)

Configuration dans `config.json` :

```json
{
  "logging": {
    "log_level": "INFO",
    "log_uart_traffic": false,
    "log_can_traffic": false
  }
}
```

### Accès aux logs

- Serial Monitor : `pio device monitor -b 115200`
- Interface web : `http://tinybms-bridge.local/api/logs`

## 🔒 Sécurité

⚠️ **IMPORTANT** : Le mot de passe WiFi est stocké en clair dans `config.json`.

Pour un environnement de production :
- Utilisez un réseau WiFi dédié
- Activez l'authentification web (optionnel dans config)
- Limitez l'accès réseau à l'ESP32

## 📊 Monitoring

### Watchdog

Le watchdog matériel redémarre l'ESP32 en cas de blocage :
- Timeout par défaut : 30 secondes
- Feed automatique par toutes les tâches critiques

### Stack monitoring

Les tâches FreeRTOS surveillent leur utilisation de pile :
```cpp
UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
```

## 🤝 Contribution

Ce projet est fonctionnel mais perfectible. Les contributions sont bienvenues pour :
- Implémentation complète du protocole Modbus RTU (TinyBMS)
- Support CAN natif ESP32 (sans MCP2515)
- Optimisations CVL
- Tests unitaires
- Documentation supplémentaire

## 📝 Licence

Projet open-source - voir conditions d'utilisation de vos bibliothèques.

## 🔗 Références

- [TinyBMS Documentation](https://www.tinybms.com)
- [Victron CAN-BMS Protocol](https://www.victronenergy.com/live/battery_compatibility:can-bus_bms-cable_bms)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/00-Overview)

## 📧 Support

Pour toute question ou problème, ouvrez une issue sur le dépôt GitHub du projet.

---

**Version** : 2.2
**Dernière mise à jour** : 2025-10-26
**Status** : ✅ Fonctionnel - Compilation OK
