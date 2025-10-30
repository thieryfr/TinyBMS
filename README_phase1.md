
### Architecture Actuelle
```
┌─────────────────────────────────────────────────────────┐
│                   Configuration Layer                    │
│         NVS (chiffré) + Config Manager (JSON)           │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│              Hardware Abstraction Layer (HAL)            │
│    Interfaces abstraites pour UART/CAN/GPIO/Storage     │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│                  Core Business Logic                     │
│         CVL Algorithm + BMS Logic + Protocols           │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│              Event Bus Générique (Templates)             │
│          Publisher/Subscriber avec types forts          │
└─────────────────────────────────────────────────────────┘
                            ↓
        ┌──────────────┬────────────┬──────────────┐
        │              │            │              │
┌───────▼──────┐ ┌─────▼─────┐ ┌───▼────┐ ┌──────▼──────┐
│ UART Driver  │ │CAN Driver │ │  Web   │ │    MQTT     │
│   avec DMA   │ │avec filter│ │ Async  │ │   Client    │
└──────────────┘ └───────────┘ └────────┘ └─────────────┘
```

## 📋 Plan par phases

### **Phase 1: HAL et Abstraction (2-3 semaines)**

#### Nouveaux fichiers à créer:
```cpp
include/hal/
├── hal_uart.h          // Interface UART abstraite
├── hal_can.h           // Interface CAN abstraite  
├── hal_storage.h       // Interface Storage (NVS/SPIFFS)
├── hal_gpio.h          // Interface GPIO abstraite
└── hal_factory.h       // Factory pour créer les implémentations

src/hal/
├── esp32/
│   ├── esp32_uart.cpp      // Implémentation ESP32 UART avec DMA
│   ├── esp32_can.cpp       // Implémentation ESP32 CAN avec filtres
│   ├── esp32_storage.cpp   // NVS + SPIFFS
│   └── esp32_gpio.cpp      // GPIO ESP32
└── mock/
    ├── mock_uart.cpp       // Mock pour tests
    ├── mock_can.cpp        // Mock pour tests
    └── mock_storage.cpp    // Mock pour tests
```

#### Fichiers à modifier:
- **src/bridge_uart.cpp** → Utiliser HAL_UART au lieu de HardwareSerial
- **src/bridge_can.cpp** → Utiliser HAL_CAN au lieu de CAN direct
- **src/config_manager.cpp** → Ajouter support NVS pour credentials
- **src/system_init.cpp** → Initialiser HAL Factory

#### Exemple de refactoring:
```cpp
// Avant (bridge_uart.cpp)
Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
Serial2.readBytes(buffer, size);

// Après
auto uart = HALFactory::createUART(UART_TINYBMS);
uart->configure({.baudrate=115200, .use_dma=true});
uart->read(buffer, size);
```

---

### **Phase 2: Event Bus Générique (2 semaines)**

#### Nouveaux fichiers:
```cpp
include/event/
├── event_bus_v2.h       // Template-based Event Bus
├── event_types_v2.h     // Types d'événements typés
└── event_subscriber.h   // Interface subscriber

src/event/
└── event_bus_v2.cpp     // Implémentation générique
```

#### Fichiers à modifier:
- **Tous les publishers** (uart, can, cvl, web) → Migrer vers EventBus v2
- **src/main.ino** → Initialiser nouveau bus

#### Exemple de migration:
```cpp
// Avant
eventBus.publishLiveData(data, SOURCE_ID_UART);

// Après  
eventBus.publish<LiveDataEvent>(data);
```

---

### **Phase 3: Configuration Sécurisée (1 semaine)**

#### Nouveaux fichiers:
```cpp
include/security/
├── credentials_manager.h   // Gestion credentials NVS
└── crypto_utils.h         // Chiffrement/déchiffrement

src/security/
├── credentials_manager.cpp
└── crypto_utils.cpp
```

#### Fichiers à modifier:
- **src/config_manager.cpp** → Séparer config et credentials
- **src/system_init.cpp** → Charger credentials depuis NVS
- **data/config.json** → Retirer passwords/clés

---

### **Phase 4: Optimisations Performance (2 semaines)**

#### Nouveaux fichiers:
```cpp
include/optimization/
├── adaptive_polling.h     // Polling UART adaptatif
├── websocket_throttle.h   // Throttling WebSocket
└── ring_buffer.h          // Buffer circulaire DMA

src/optimization/
├── adaptive_polling.cpp
├── websocket_throttle.cpp
└── ring_buffer.cpp
```

#### Fichiers à modifier:
- **src/bridge_uart.cpp** → Implémenter polling adaptatif
- **src/websocket_handlers.cpp** → Ajouter throttling
- **src/bridge_can.cpp** → Utiliser filtres hardware

---

### **Phase 5: Tests et CI/CD (1 semaine)**

#### Nouveaux fichiers:
```yaml
.github/workflows/
├── build.yml          // Build PlatformIO
├── test.yml           // Tests unitaires
└── release.yml        // Release avec OTA

tests/unit/
├── test_hal_uart.cpp
├── test_hal_can.cpp
├── test_event_bus_v2.cpp
└── test_credentials.cpp
```

#### Fichiers à modifier:
- **platformio.ini** → Ajouter environnements de test
- **CMakeLists.txt** → Configuration pour tests natifs

---

## 🎯 Alternative: Implémentation en une seule phase (4-5 semaines)

Si vous préférez une refonte complète en une fois:

### Ordre d'implémentation recommandé:
1. **Semaine 1**: HAL complet + mocks
2. **Semaine 2**: Event Bus v2 + migration
3. **Semaine 3**: Configuration sécurisée + optimisations
4. **Semaine 4**: Tests complets + CI/CD
5. **Semaine 5**: Validation terrain + documentation

### Fichiers critiques à toucher (par ordre de priorité):

1. **Création HAL** (20 fichiers)
   - Nouveaux: `hal/*.h`, `hal/esp32/*.cpp`, `hal/mock/*.cpp`

2. **Refactoring Core** (15 fichiers)
   - `bridge_uart.cpp`, `bridge_can.cpp`, `system_init.cpp`
   - `config_manager.cpp`, `main.ino`

3. **Event Bus v2** (10 fichiers)  
   - Nouveaux: `event_bus_v2.h/cpp`
   - Tous les publishers/subscribers

4. **Sécurité** (5 fichiers)
   - Nouveaux: `credentials_manager.*`, `crypto_utils.*`
   - `config_manager.cpp`

5. **Tests** (15 fichiers)
   - Tous les `test_*.cpp`
   - `.github/workflows/*`

### Risques et mitigation:
- **Risque**: Régression fonctionnelle → **Mitigation**: Tests E2E avant/après
- **Risque**: Incompatibilité terrain → **Mitigation**: Branch feature + beta testing
- **Risque**: Performance dégradée → **Mitigation**: Profiling FreeRTOS

### Recommandation finale:
**Approche par phases** recommandée pour:
- Validation progressive
- Rollback facilité
- Tests incrémentaux
- Continuité de service

La **Phase 1 (HAL)** est la plus critique car elle pose les fondations. Une fois en place, les autres phases peuvent être parallélisées par différents développeurs.

# 📋 Plan Détaillé - Phase 1: Hardware Abstraction Layer (HAL)

## 🎯 Objectifs de la Phase 1

### Vision
Créer une couche d'abstraction matérielle (HAL) qui découple complètement la logique métier du hardware ESP32, permettant une meilleure testabilité, portabilité et maintenabilité du code.

### Problèmes actuels à résoudre
1. **Couplage fort au hardware** - Code directement lié aux APIs ESP32 (Serial2, CAN, SPIFFS)
2. **Tests impossibles sans matériel** - Pas de mocks pour UART/CAN/Storage
3. **Gestion mémoire inefficace** - Pas d'utilisation DMA pour UART
4. **Configuration hardware éparpillée** - Pins et paramètres dispersés dans le code
5. **Difficile portabilité** - Migration vers autre MCU nécessiterait refonte complète

### Améliorations attendues
- **Testabilité**: Tests unitaires possibles avec mocks (couverture +60%)
- **Performance UART**: DMA réduira charge CPU de ~30%
- **Performance CAN**: Filtres hardware réduiront interruptions de ~50%
- **Réutilisabilité**: Code métier portable sur STM32/RP2040
- **Maintenabilité**: Configuration hardware centralisée
- **Sécurité**: Credentials isolés dans NVS avec chiffrement

---

## 🏗️ Architecture HAL Proposée

```cpp
┌─────────────────────────────────────────────┐
│            Application Layer                 │
│  (bridge_uart, bridge_can, config_manager)  │
└──────────────────┬──────────────────────────┘
                   │ Utilise
                   ▼
┌─────────────────────────────────────────────┐
│         HAL Interface Layer                  │
│   (Interfaces pures virtuelles C++)          │
├─────────────────────────────────────────────┤
│ • IHAL_UART    • IHAL_CAN                   │
│ • IHAL_Storage • IHAL_GPIO                  │
│ • IHAL_Timer   • IHAL_Watchdog              │
└──────────────────┬──────────────────────────┘
                   │ Implémente
                   ▼
┌─────────────────────────────────────────────┐
│     HAL Implementation Layer                 │
├──────────────┬──────────────────────────────┤
│   ESP32      │           Mock               │
├──────────────┼──────────────────────────────┤
│ ESP32_UART   │      Mock_UART               │
│ ESP32_CAN    │      Mock_CAN                │
│ ESP32_Storage│      Mock_Storage            │
└──────────────┴──────────────────────────────┘
```

---

## 📁 Structure des Fichiers

### Nouveaux fichiers à créer (28 fichiers)

```
include/hal/
├── interfaces/
│   ├── ihal_uart.h          [150 lignes] Interface UART
│   ├── ihal_can.h           [120 lignes] Interface CAN  
│   ├── ihal_storage.h       [100 lignes] Interface Storage
│   ├── ihal_gpio.h          [80 lignes]  Interface GPIO
│   ├── ihal_timer.h         [60 lignes]  Interface Timer
│   └── ihal_watchdog.h      [50 lignes]  Interface Watchdog
├── hal_factory.h            [100 lignes] Factory Pattern
├── hal_config.h             [80 lignes]  Configuration types
└── hal_types.h              [150 lignes] Types communs

src/hal/
├── esp32/
│   ├── esp32_uart.cpp       [400 lignes] UART avec DMA
│   ├── esp32_can.cpp        [350 lignes] CAN avec filtres
│   ├── esp32_storage.cpp    [300 lignes] NVS + SPIFFS
│   ├── esp32_gpio.cpp       [150 lignes] GPIO ESP32
│   ├── esp32_timer.cpp      [100 lignes] Timer hardware
│   └── esp32_watchdog.cpp   [80 lignes]  WDT ESP32
├── mock/
│   ├── mock_uart.cpp        [250 lignes] Mock UART
│   ├── mock_can.cpp         [200 lignes] Mock CAN
│   ├── mock_storage.cpp     [200 lignes] Mock Storage
│   ├── mock_gpio.cpp        [100 lignes] Mock GPIO
│   └── mock_timer.cpp       [80 lignes]  Mock Timer
├── hal_factory.cpp          [200 lignes] Implémentation Factory
└── hal_manager.cpp          [150 lignes] Gestionnaire HAL

tests/unit/hal/
├── test_hal_uart.cpp        [300 lignes] Tests UART
├── test_hal_can.cpp         [250 lignes] Tests CAN
├── test_hal_storage.cpp     [200 lignes] Tests Storage
└── test_hal_factory.cpp     [150 lignes] Tests Factory
```

### Fichiers existants à modifier (12 fichiers)

```
src/
├── bridge_uart.cpp          [~100 lignes modifiées]
├── bridge_can.cpp           [~150 lignes modifiées]
├── config_manager.cpp       [~80 lignes modifiées]
├── system_init.cpp          [~120 lignes modifiées]
├── main.ino                 [~50 lignes modifiées]
├── watchdog_manager.cpp     [~40 lignes modifiées]
└── can_driver.cpp           [Supprimé - remplacé par HAL]

include/
├── bridge_uart.h            [~20 lignes modifiées]
├── bridge_can.h             [~20 lignes modifiées]
├── config_manager.h         [~30 lignes modifiées]
└── shared_data.h            [~20 lignes ajoutées]

platformio.ini               [~10 lignes ajoutées]
```

---

## 📝 Implémentation Détaillée

### Semaine 1: Interfaces et Factory (Jours 1-5)

#### Jour 1-2: Création des interfaces HAL#### Jour 3: Factory Pattern et Configuration#### Jour 4-5: Implémentation ESP32### Semaine 2: Intégration et Refactoring (Jours 6-10)

#### Jour 6-7: Refactoring bridge_uart.cpp#### Jour 8: Tests unitaires avec Mocks#### Jour 9-10: Documentation et CI/CD## 📊 Résumé du Plan Phase 1

### Objectifs atteints
✅ **Architecture HAL définie** avec interfaces claires  
✅ **28 nouveaux fichiers** structurés (interfaces, implémentations, tests)  
✅ **Guide de migration** détaillé avec exemples concrets  
✅ **Tests unitaires** avec framework mock  
✅ **Documentation complète** pour faciliter l'adoption  

### Livrables Phase 1

#### Semaine 1 (Jours 1-5)
- Interfaces HAL (UART, CAN, Storage, GPIO, Timer, Watchdog)
- HAL Factory avec pattern Singleton
- Types communs et configuration
- Structure de base ESP32 et Mock

#### Semaine 2 (Jours 6-10)
- Refactoring bridge_uart avec HAL
- Refactoring bridge_can avec HAL
- Migration config_manager (NVS sécurisé)
- Tests unitaires complets
- Documentation et guide de migration

### Impact sur le code existant

| Module | Lignes modifiées | Complexité | Risque |
|--------|-----------------|------------|---------|
| bridge_uart.cpp | ~100 | Moyenne | Faible |
| bridge_can.cpp | ~150 | Moyenne | Faible |
| config_manager.cpp | ~80 | Haute | Moyen |
| system_init.cpp | ~120 | Moyenne | Faible |
| main.ino | ~50 | Faible | Faible |

### Métriques de succès mesurables

1. **Performance**
   - DMA UART: -30% charge CPU
   - Filtres CAN: -50% interruptions
   - Latence Modbus: <5ms constant

2. **Qualité**
   - Tests unitaires: +50 nouveaux tests
   - Couverture: 20% → 80%
   - Bugs détectés: estimation +15 avant production

3. **Maintenabilité**
   - Code dupliqué: -40%
   - Dépendances hardware: 100% isolées
   - Portabilité: 3 plateformes supportées

### Validation et rollback

**Plan de validation:**
1. Tests unitaires HAL (2h)
2. Tests intégration sur ESP32 dev board (4h)
3. Tests terrain avec BMS réel (8h)
4. Validation performance avec oscilloscope (2h)

**Plan de rollback:**
- Branch Git séparée `feature/hal-phase1`
- Tags avant/après migration
- Possibilité de compilation conditionnelle: `#ifdef USE_HAL`

Cette Phase 1 pose les fondations solides pour les améliorations futures tout en minimisant les risques grâce à une approche incrémentale et bien testée.
