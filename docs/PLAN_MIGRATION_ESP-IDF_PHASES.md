# Plan de Migration ESP-IDF - Approche Progressive Sans Rupture

**Projet:** TinyBMS-Victron Bridge v2.5.0 → v3.0.0 (ESP-IDF natif)
**Date:** 2025-10-30
**Statut:** Proposition - En attente de validation
**Auteur:** Migration Planning Team

---

## 🎯 Objectifs Globaux

### Objectifs Fonctionnels
1. **Migration complète** vers ESP-IDF natif (sans framework Arduino)
2. **Préservation stricte** de toutes les fonctionnalités actuelles
3. **Maintien ou amélioration** des performances actuelles
4. **Compatibilité ascendante** : configuration, API REST, protocoles CAN/UART

### Objectifs Non-Fonctionnels
1. **Aucune rupture** de compatibilité pour l'utilisateur final
2. **Coexistence** temporaire PlatformIO/Arduino + ESP-IDF
3. **Rollback possible** à chaque phase
4. **Documentation exhaustive** de chaque changement

---

## 📊 État Actuel vs Cible

| Aspect | État Actuel | Cible ESP-IDF | Compatibilité |
|--------|-------------|---------------|---------------|
| **Build System** | PlatformIO + Arduino | ESP-IDF + CMake | ⚠️ Dual build temporaire |
| **Framework** | Arduino wrapper | ESP-IDF natif | ❌ Incompatible (migration requise) |
| **UART** | Arduino Serial | esp_uart driver | ✅ Via HAL (transparent) |
| **CAN** | sandeepmistry/CAN | twai_driver natif | ✅ Via HAL (transparent) |
| **WiFi** | Arduino WiFi | esp_wifi + netif | ✅ Config JSON identique |
| **SPIFFS** | Arduino SPIFFS | esp_spiffs + VFS | ✅ Fichiers compatibles |
| **WebServer** | ESPAsyncWebServer | esp_http_server | ⚠️ API interne change, endpoints identiques |
| **WebSocket** | AsyncWebSocket | esp_http_server WS | ⚠️ Protocole identique, impl différente |
| **JSON** | ArduinoJson 6.21 | ArduinoJson 6.21 | ✅ Aucun changement |
| **FreeRTOS** | Via Arduino | Direct ESP-IDF | ✅ Déjà utilisé directement |
| **Logging** | Serial + SPIFFS | esp_log + SPIFFS | ⚠️ Format différent, niveaux compatibles |

### Métriques de Performance à Préserver

| Métrique | Valeur Actuelle | Seuil Minimum Acceptable |
|----------|----------------|--------------------------|
| **Latence UART→CAN** | 70-90ms | < 150ms |
| **Latence WebSocket** | 80-120ms | < 200ms |
| **Latence CAN TX** | 2-5ms | < 10ms |
| **Heap libre** | 180-220 KB | > 150 KB |
| **Charge CPU** | 15-25% | < 60% |
| **Taille binaire** | ~500 KB | < 1 MB |
| **Uptime MTBF** | > 24h stable | > 24h stable |

---

## 🔄 Architecture de Migration - Vision d'Ensemble

### Principe Directeur : **HAL First, Progressive Replacement**

```
┌─────────────────────────────────────────────────────────────────┐
│                    PHASE 0 : État Actuel                         │
│  PlatformIO + Arduino Framework + ESPAsyncWebServer              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              PHASE 1 : Fondations ESP-IDF (Dual Build)           │
│  PlatformIO(main) + ESP-IDF(parallel) + HAL IDF + Tests          │
│  ✅ Critère: Build ESP-IDF fonctionnel, Arduino inchangé        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│         PHASE 2 : Migration Périphériques (HAL Swap)             │
│  ESP-IDF + HAL IDF (UART/CAN/Storage) + Arduino WebServer        │
│  ✅ Critère: Périphériques fonctionnels, API REST identique     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│          PHASE 3 : Migration WebServer (Risque Max)              │
│  ESP-IDF + esp_http_server + WebSocket natif + Tests stress      │
│  ✅ Critère: Tous endpoints + WS OK, latence acceptable         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│           PHASE 4 : Optimisation & Suppression Arduino           │
│  ESP-IDF pur + esp_log + optimisations + documentation finale    │
│  ✅ Critère: 24h stability + perf ≥ baseline + docs complètes   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📋 PHASE 1 : Fondations ESP-IDF (1-2 semaines)

### 🎯 Objectif
Créer l'infrastructure ESP-IDF en parallèle du système Arduino existant, sans modifier le code de production.

### 📦 Livrables

#### 1.1 Système de Build ESP-IDF
**Fichiers à créer:**
```
TinyBMS/
├── CMakeLists.txt                    # Build racine ESP-IDF
├── main/
│   ├── CMakeLists.txt                # Build composant principal
│   └── idf_component.yml             # Dépendances ESP-IDF
├── components/
│   ├── hal_idf/                      # Nouveau composant HAL ESP-IDF
│   │   ├── CMakeLists.txt
│   │   └── esp32_*_idf.cpp           # Implémentations IDF
│   ├── event_bus/                    # EventBus comme composant
│   │   └── CMakeLists.txt
│   └── arduinojson/                  # ArduinoJson comme composant
│       └── CMakeLists.txt
└── sdkconfig.defaults                # Configuration ESP-IDF par défaut
```

**Configuration CMake Racine:**
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tinybms_bridge)

# Dépendances ESP-IDF
set(EXTRA_COMPONENT_DIRS components)
```

**Configuration sdkconfig.defaults:**
```ini
# Performances
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y
CONFIG_COMPILER_OPTIMIZATION_SIZE=n
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# UART
CONFIG_UART_ISR_IN_IRAM=y

# WiFi
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP32_WIFI_STATIC_TX_BUFFER_NUM=0
CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM=32

# CAN (TWAI)
CONFIG_TWAI_ISR_IN_IRAM=y
CONFIG_TWAI_ERRATA_FIX_RX_FRAME_INVALID=y
CONFIG_TWAI_ERRATA_FIX_TX_INTR_LOST=y

# SPIFFS
CONFIG_SPIFFS_MAX_PARTITIONS=1
CONFIG_SPIFFS_CACHE=y
CONFIG_SPIFFS_CACHE_WR=y

# Logging
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_COLORS=y

# Stack sizes
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
```

#### 1.2 HAL ESP-IDF - Implémentations Natives

**1.2.1 UART HAL (`components/hal_idf/esp32_uart_idf.cpp`)**
```cpp
#include "hal/i_hal_uart.h"
#include "driver/uart.h"
#include "esp_log.h"

class ESP32UartIDF : public IHalUart {
private:
    uart_port_t uart_num_;
    int rx_pin_;
    int tx_pin_;
    int baudrate_;

    static const char* TAG;
    static const int RX_BUFFER_SIZE = 1024;
    static const int TX_BUFFER_SIZE = 1024;

public:
    ESP32UartIDF(int uart_num, int rx_pin, int tx_pin, int baudrate)
        : uart_num_(static_cast<uart_port_t>(uart_num))
        , rx_pin_(rx_pin)
        , tx_pin_(tx_pin)
        , baudrate_(baudrate) {}

    bool begin() override {
        uart_config_t uart_config = {
            .baud_rate = baudrate_,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
        };

        ESP_ERROR_CHECK(uart_param_config(uart_num_, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(uart_num_, tx_pin_, rx_pin_,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(uart_num_, RX_BUFFER_SIZE,
                                            TX_BUFFER_SIZE, 0, NULL, 0));

        ESP_LOGI(TAG, "UART%d initialized: RX=%d, TX=%d, baud=%d",
                 uart_num_, rx_pin_, tx_pin_, baudrate_);
        return true;
    }

    size_t write(const uint8_t* data, size_t len) override {
        int written = uart_write_bytes(uart_num_, (const char*)data, len);
        return written >= 0 ? written : 0;
    }

    size_t available() override {
        size_t available = 0;
        uart_get_buffered_data_len(uart_num_, &available);
        return available;
    }

    int read() override {
        uint8_t byte;
        int len = uart_read_bytes(uart_num_, &byte, 1, 0);
        return len > 0 ? byte : -1;
    }

    size_t readBytes(uint8_t* buffer, size_t length, uint32_t timeout_ms) override {
        int len = uart_read_bytes(uart_num_, buffer, length,
                                  pdMS_TO_TICKS(timeout_ms));
        return len >= 0 ? len : 0;
    }

    void flush() override {
        uart_wait_tx_done(uart_num_, portMAX_DELAY);
    }
};

const char* ESP32UartIDF::TAG = "ESP32UartIDF";
```

**✅ Critère de Non-Rupture UART:**
- Interface `IHalUart` strictement respectée
- Comportement identique à l'implémentation Arduino actuelle
- Timeouts, baudrate, buffer sizes configurables
- Logs compatibles (niveaux: ERROR, WARN, INFO, DEBUG)

**1.2.2 CAN HAL (`components/hal_idf/esp32_can_idf.cpp`)**
```cpp
#include "hal/i_hal_can.h"
#include "driver/twai.h"
#include "esp_log.h"

class ESP32CanIDF : public IHalCan {
private:
    int tx_pin_;
    int rx_pin_;
    uint32_t bitrate_;
    bool initialized_;

    static const char* TAG;

public:
    ESP32CanIDF(int tx_pin, int rx_pin, uint32_t bitrate)
        : tx_pin_(tx_pin)
        , rx_pin_(rx_pin)
        , bitrate_(bitrate)
        , initialized_(false) {}

    bool begin() override {
        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
            static_cast<gpio_num_t>(tx_pin_),
            static_cast<gpio_num_t>(rx_pin_),
            TWAI_MODE_NORMAL
        );

        // Configuration bitrate (500 kbps pour Victron)
        twai_timing_config_t t_config;
        if (bitrate_ == 500000) {
            t_config = TWAI_TIMING_CONFIG_500KBITS();
        } else if (bitrate_ == 250000) {
            t_config = TWAI_TIMING_CONFIG_250KBITS();
        } else {
            ESP_LOGE(TAG, "Unsupported bitrate: %lu", bitrate_);
            return false;
        }

        twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TWAI driver install failed: %s", esp_err_to_name(err));
            return false;
        }

        err = twai_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
            twai_driver_uninstall();
            return false;
        }

        initialized_ = true;
        ESP_LOGI(TAG, "CAN initialized: TX=%d, RX=%d, bitrate=%lu",
                 tx_pin_, rx_pin_, bitrate_);
        return true;
    }

    bool sendMessage(uint32_t id, const uint8_t* data, uint8_t len, bool extended) override {
        if (!initialized_) {
            ESP_LOGE(TAG, "CAN not initialized");
            return false;
        }

        twai_message_t message = {};
        message.identifier = id;
        message.data_length_code = len;
        message.extd = extended ? 1 : 0;
        memcpy(message.data, data, len);

        esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(10));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "CAN TX failed: %s", esp_err_to_name(err));
            return false;
        }

        return true;
    }

    bool receiveMessage(uint32_t* id, uint8_t* data, uint8_t* len,
                       bool* extended, uint32_t timeout_ms) override {
        if (!initialized_) return false;

        twai_message_t message;
        esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(timeout_ms));
        if (err != ESP_OK) return false;

        *id = message.identifier;
        *len = message.data_length_code;
        *extended = message.extd;
        memcpy(data, message.data, message.data_length_code);

        return true;
    }
};

const char* ESP32CanIDF::TAG = "ESP32CanIDF";
```

**✅ Critère de Non-Rupture CAN:**
- Interface `IHalCan` strictement respectée
- Support des mêmes bitrates (250k, 500k)
- Messages CAN identiques (ID, data, extended frame)
- Gestion erreurs compatible

**1.2.3 Storage HAL (`components/hal_idf/esp32_storage_idf.cpp`)**
```cpp
#include "hal/i_hal_storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <fstream>
#include <sys/stat.h>

class ESP32StorageIDF : public IHalStorage {
private:
    static const char* TAG;
    static const char* BASE_PATH;
    bool initialized_;

public:
    ESP32StorageIDF() : initialized_(false) {}

    bool begin() override {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = BASE_PATH,
            .partition_label = NULL,
            .max_files = 5,
            .format_if_mount_failed = false
        };

        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount SPIFFS");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "SPIFFS partition not found");
            }
            return false;
        }

        size_t total = 0, used = 0;
        ret = esp_spiffs_info(NULL, &total, &used);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS: total=%zu KB, used=%zu KB",
                     total / 1024, used / 1024);
        }

        initialized_ = true;
        return true;
    }

    bool exists(const char* path) override {
        std::string fullPath = std::string(BASE_PATH) + path;
        struct stat st;
        return stat(fullPath.c_str(), &st) == 0;
    }

    bool readFile(const char* path, std::string& content) override {
        std::string fullPath = std::string(BASE_PATH) + path;
        std::ifstream file(fullPath);
        if (!file.is_open()) {
            ESP_LOGW(TAG, "Failed to open file: %s", fullPath.c_str());
            return false;
        }

        content.assign((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
        return true;
    }

    bool writeFile(const char* path, const std::string& content) override {
        std::string fullPath = std::string(BASE_PATH) + path;
        std::ofstream file(fullPath);
        if (!file.is_open()) {
            ESP_LOGE(TAG, "Failed to open file for writing: %s", fullPath.c_str());
            return false;
        }

        file << content;
        return file.good();
    }

    bool deleteFile(const char* path) override {
        std::string fullPath = std::string(BASE_PATH) + path;
        return unlink(fullPath.c_str()) == 0;
    }
};

const char* ESP32StorageIDF::TAG = "ESP32StorageIDF";
const char* ESP32StorageIDF::BASE_PATH = "/spiffs";
```

**✅ Critère de Non-Rupture Storage:**
- Filesystem SPIFFS compatible (fichiers existants lisibles)
- Chemins identiques (`/config.json`, `/logs.txt`, etc.)
- API read/write/delete identique
- Format JSON inchangé

**1.2.4 WiFi HAL (`components/hal_idf/esp32_wifi_idf.cpp`)**
```cpp
#include "hal/i_hal_wifi.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include <cstring>

class ESP32WiFiIDF : public IHalWifi {
private:
    static const char* TAG;
    static const int WIFI_CONNECTED_BIT = BIT0;
    static const int WIFI_FAIL_BIT = BIT1;

    EventGroupHandle_t wifi_event_group_;
    esp_netif_t* sta_netif_;
    esp_netif_t* ap_netif_;
    bool initialized_;
    int retry_num_;

    static void event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data) {
        ESP32WiFiIDF* self = static_cast<ESP32WiFiIDF*>(arg);

        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (self->retry_num_ < 10) {
                esp_wifi_connect();
                self->retry_num_++;
                ESP_LOGI(TAG, "Retry to connect to the AP");
            } else {
                xEventGroupSetBits(self->wifi_event_group_, WIFI_FAIL_BIT);
            }
            ESP_LOGI(TAG, "Connect to the AP failed");
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
            self->retry_num_ = 0;
            xEventGroupSetBits(self->wifi_event_group_, WIFI_CONNECTED_BIT);
        }
    }

public:
    ESP32WiFiIDF()
        : wifi_event_group_(nullptr)
        , sta_netif_(nullptr)
        , ap_netif_(nullptr)
        , initialized_(false)
        , retry_num_(0) {}

    bool beginSTA(const char* ssid, const char* password,
                  const char* hostname) override {
        if (!initialized_) {
            ESP_ERROR_CHECK(esp_netif_init());
            ESP_ERROR_CHECK(esp_event_loop_create_default());
            wifi_event_group_ = xEventGroupCreate();
            initialized_ = true;
        }

        sta_netif_ = esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, &instance_got_ip));

        wifi_config_t wifi_config = {};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        if (hostname) {
            esp_netif_set_hostname(sta_netif_, hostname);
        }

        ESP_LOGI(TAG, "WiFi STA initialized. SSID:%s", ssid);

        // Wait for connection
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group_,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

        return (bits & WIFI_CONNECTED_BIT) != 0;
    }

    bool isConnected() override {
        if (!wifi_event_group_) return false;
        EventBits_t bits = xEventGroupGetBits(wifi_event_group_);
        return (bits & WIFI_CONNECTED_BIT) != 0;
    }

    std::string getLocalIP() override {
        if (!sta_netif_) return "0.0.0.0";

        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(sta_netif_, &ip_info);

        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        return std::string(ip_str);
    }
};

const char* ESP32WiFiIDF::TAG = "ESP32WiFiIDF";
```

**✅ Critère de Non-Rupture WiFi:**
- Configuration identique (SSID, password, hostname depuis config.json)
- Mode STA identique
- Retry logic compatible
- mDNS hostname identique

#### 1.3 Tests Unitaires HAL ESP-IDF

**Créer:** `tests/test_hal_idf/test_uart_idf.cpp`
```cpp
#include "unity.h"
#include "esp32_uart_idf.h"

TEST_CASE("UART IDF initialization", "[hal][uart]") {
    ESP32UartIDF uart(2, 16, 17, 19200);
    TEST_ASSERT_TRUE(uart.begin());
}

TEST_CASE("UART IDF loopback", "[hal][uart]") {
    ESP32UartIDF uart(2, 16, 17, 19200);
    uart.begin();

    const char* msg = "TEST";
    size_t written = uart.write((const uint8_t*)msg, strlen(msg));
    TEST_ASSERT_EQUAL(strlen(msg), written);

    vTaskDelay(pdMS_TO_TICKS(100));

    TEST_ASSERT_TRUE(uart.available() > 0);
}
```

**Créer:** Tests similaires pour CAN, Storage, WiFi

#### 1.4 Factory ESP-IDF

**Créer:** `components/hal_idf/esp32_factory_idf.cpp`
```cpp
#include "hal/i_hal_factory.h"
#include "esp32_uart_idf.h"
#include "esp32_can_idf.h"
#include "esp32_storage_idf.h"
#include "esp32_wifi_idf.h"
// ... autres inclusions

class ESP32FactoryIDF : public IHalFactory {
public:
    std::unique_ptr<IHalUart> createUart(int rx, int tx, int baud) override {
        return std::make_unique<ESP32UartIDF>(2, rx, tx, baud);
    }

    std::unique_ptr<IHalCan> createCan(int tx, int rx, uint32_t bitrate) override {
        return std::make_unique<ESP32CanIDF>(tx, rx, bitrate);
    }

    std::unique_ptr<IHalStorage> createStorage() override {
        return std::make_unique<ESP32StorageIDF>();
    }

    std::unique_ptr<IHalWifi> createWifi() override {
        return std::make_unique<ESP32WiFiIDF>();
    }

    // ... autres méthodes
};
```

### ✅ Critères de Validation Phase 1

| # | Critère | Méthode de Validation | Seuil Acceptation |
|---|---------|----------------------|-------------------|
| 1.1 | **Build ESP-IDF réussit** | `idf.py build` | Succès, 0 erreurs |
| 1.2 | **Build Arduino inchangé** | `pio run` | Succès, même binaire |
| 1.3 | **Tests HAL IDF passent** | `idf.py test` | 100% pass |
| 1.4 | **Taille binaire** | `idf.py size` | < 1 MB |
| 1.5 | **Heap disponible** | Tests boot | > 200 KB |
| 1.6 | **Flash ESP32 OK** | `idf.py flash monitor` | Boot sans erreur |
| 1.7 | **UART loopback** | Test matériel | RX/TX fonctionnel |
| 1.8 | **CAN loopback** | Test matériel | TX/RX fonctionnel |
| 1.9 | **SPIFFS mount** | Lecture config.json | Fichier lu correctement |
| 1.10 | **WiFi STA connect** | Connexion réseau | IP obtenue, mDNS OK |

### 🔄 Rollback Phase 1
**Condition:** Si critère 1.1-1.10 échoue
**Action:** Supprimer fichiers ESP-IDF (CMakeLists.txt, components/), continuer avec PlatformIO

### 📊 Effort Estimé Phase 1
- **Durée:** 1-2 semaines
- **Charge:** 5-10 jours/homme
- **Ressources:** 1 développeur ESP-IDF
- **Parallélisation:** Possible (n'impacte pas production)

---

## 📋 PHASE 2 : Migration Périphériques via HAL (1-2 semaines)

### 🎯 Objectif
Basculer les périphériques (UART, CAN, Storage, WiFi) vers HAL ESP-IDF tout en maintenant le reste du système Arduino.

### 📦 Livrables

#### 2.1 Switch HAL Factory

**Modifier:** `src/main.ino` ou équivalent ESP-IDF `main/main.cpp`
```cpp
// AVANT (Phase 1)
#include "hal/esp32/esp32_factory.h"
HalManager::getInstance().setFactory(std::make_unique<ESP32Factory>());

// APRÈS (Phase 2)
#include "hal_idf/esp32_factory_idf.h"
HalManager::getInstance().setFactory(std::make_unique<ESP32FactoryIDF>());
```

**✅ Impact:** Aucun changement de code dans les modules métier (bridge_uart, bridge_can, etc.)
**Raison:** HAL abstraction isole la logique métier des détails d'implémentation

#### 2.2 Migration Logging vers esp_log

**Créer:** `src/logger_idf.cpp`
```cpp
#include "logger.h"
#include "esp_log.h"

// Mapping niveaux Logger → esp_log
static esp_log_level_t mapLogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR: return ESP_LOG_ERROR;
        case LogLevel::WARNING: return ESP_LOG_WARN;
        case LogLevel::INFO: return ESP_LOG_INFO;
        case LogLevel::DEBUG: return ESP_LOG_DEBUG;
        default: return ESP_LOG_INFO;
    }
}

void Logger::log(LogLevel level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    esp_log_writev(mapLogLevel(level), tag, format, args);
    va_end(args);

    // Optionnel: conserver logging SPIFFS
    if (spiffsLoggingEnabled_) {
        logToSPIFFS(level, tag, format, args);
    }
}
```

**✅ Critère de Non-Rupture Logging:**
- API `Logger::log()` inchangée
- Niveaux de log identiques (ERROR, WARNING, INFO, DEBUG)
- SPIFFS logging optionnel conservé
- Format de sortie similaire (timestamp, tag, message)

#### 2.3 Tests End-to-End avec HAL IDF

**Tester:**
1. **UART → EventBus → CAN** (flux complet)
2. **Config JSON reload** (SPIFFS read/write)
3. **WiFi reconnect** (après déconnexion)
4. **WebSocket broadcast** (via Arduino AsyncWebServer temporairement)

### ✅ Critères de Validation Phase 2

| # | Critère | Méthode de Validation | Seuil Acceptation |
|---|---------|----------------------|-------------------|
| 2.1 | **UART polling fonctionne** | Logs "UART poll success" | > 95% success rate |
| 2.2 | **CAN TX réussit** | Logs "CAN TX" | 100% frames envoyés |
| 2.3 | **Latence UART→CAN** | Logs timestamps | < 150ms (P95) |
| 2.4 | **Config JSON reload** | POST /api/settings | Succès, config appliquée |
| 2.5 | **WiFi reconnect** | Déconnexion → reconnexion | < 10s |
| 2.6 | **SPIFFS read/write** | Logs persistés | Fichier logs.txt OK |
| 2.7 | **EventBus stats** | GET /api/diagnostics | queue_overruns = 0 |
| 2.8 | **Heap stable** | Monitor 1h | > 150 KB constant |
| 2.9 | **Watchdog OK** | Run 1h | Aucun reset |
| 2.10 | **API REST répond** | GET /api/status | JSON valide |

### 🚨 Points d'Attention

**Attention 1: UART Timing**
- TWAI driver peut introduire latence différente
- **Mitigation:** Profiler avec esp_timer_get_time() avant/après
- **Seuil:** Si latence > 150ms, revoir buffer sizes TWAI

**Attention 2: CAN Bus Errors**
- twai_driver gère erreurs différemment
- **Mitigation:** Logger tous les `twai_get_status_info()` warnings
- **Seuil:** Si error_count > 10/h, vérifier terminaisons bus

**Attention 3: WiFi Stability**
- esp_wifi peut déconnecter différemment
- **Mitigation:** Implementer retry logic robuste
- **Seuil:** Max 3 reconnexions par heure

### 🔄 Rollback Phase 2
**Condition:** Si critère 2.1-2.10 échoue
**Action:** Revenir à `ESP32Factory` (Arduino HAL), conserver ESP-IDF build

### 📊 Effort Estimé Phase 2
- **Durée:** 1-2 semaines
- **Charge:** 5-8 jours/homme
- **Ressources:** 1 développeur + 1 testeur hardware
- **Parallélisation:** Non (critique path)

---

## 📋 PHASE 3 : Migration WebServer vers esp_http_server (2-3 semaines)

### 🎯 Objectif
Remplacer ESPAsyncWebServer par esp_http_server natif ESP-IDF, en préservant tous les endpoints API et WebSocket.

### ⚠️ RISQUE MAJEUR : Cette phase est la plus critique

**Pourquoi critique:**
1. **ESPAsyncWebServer** est le cœur de l'interface utilisateur
2. **WebSocket** est temps-réel (broadcast 1Hz)
3. **Multiples clients** (jusqu'à 4 simultanés)
4. **Latence sensible** (actuel: 80-120ms)

### 📦 Livrables

#### 3.1 Wrapper esp_http_server

**Créer:** `components/webserver_idf/http_server_wrapper.cpp`
```cpp
#include "esp_http_server.h"
#include "esp_log.h"
#include <functional>
#include <map>

class HttpServerIDF {
private:
    httpd_handle_t server_;
    static const char* TAG;

    struct RouteHandler {
        std::function<esp_err_t(httpd_req_t*)> handler;
    };

    std::map<std::string, RouteHandler> routes_;

    static esp_err_t route_dispatch(httpd_req_t *req) {
        HttpServerIDF* self = (HttpServerIDF*)req->user_ctx;
        std::string uri(req->uri);

        auto it = self->routes_.find(uri);
        if (it != self->routes_.end()) {
            return it->second.handler(req);
        }

        httpd_resp_send_404(req);
        return ESP_OK;
    }

public:
    HttpServerIDF() : server_(nullptr) {}

    bool begin(uint16_t port = 80) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = port;
        config.max_uri_handlers = 32;
        config.max_resp_headers = 16;
        config.stack_size = 8192;
        config.task_priority = 5;
        config.core_id = 0; // Core 0 pour web, Core 1 pour CAN/UART

        esp_err_t err = httpd_start(&server_, &config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
            return false;
        }

        ESP_LOGI(TAG, "HTTP server started on port %d", port);
        return true;
    }

    void on(const char* uri, httpd_method_t method,
            std::function<esp_err_t(httpd_req_t*)> handler) {
        httpd_uri_t uri_handler = {
            .uri = uri,
            .method = method,
            .handler = route_dispatch,
            .user_ctx = this
        };

        routes_[uri] = {handler};
        httpd_register_uri_handler(server_, &uri_handler);
    }

    static esp_err_t sendJSON(httpd_req_t *req, const std::string& json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        return httpd_resp_send(req, json.c_str(), json.length());
    }
};

const char* HttpServerIDF::TAG = "HttpServerIDF";
```

#### 3.2 WebSocket Handler ESP-IDF

**Créer:** `components/webserver_idf/websocket_handler_idf.cpp`
```cpp
#include "esp_http_server.h"
#include "esp_log.h"
#include <vector>
#include <mutex>

class WebSocketHandlerIDF {
private:
    static const char* TAG;
    static const int MAX_CLIENTS = 4;

    struct Client {
        int fd;
        uint64_t last_ping;
    };

    std::vector<Client> clients_;
    std::mutex clients_mutex_;
    httpd_handle_t server_;

    static esp_err_t ws_handler(httpd_req_t *req) {
        if (req->method == HTTP_GET) {
            ESP_LOGI(TAG, "WebSocket handshake");
            return ESP_OK;
        }

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
            return ret;
        }

        if (ws_pkt.len) {
            uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
            if (buf == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory");
                return ESP_ERR_NO_MEM;
            }
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
                free(buf);
                return ret;
            }

            // Process message (ping/pong, commands, etc.)

            free(buf);
        }

        return ESP_OK;
    }

public:
    WebSocketHandlerIDF(httpd_handle_t server) : server_(server) {}

    bool registerEndpoint(const char* uri) {
        httpd_uri_t ws = {
            .uri = uri,
            .method = HTTP_GET,
            .handler = ws_handler,
            .user_ctx = this,
            .is_websocket = true
        };

        return httpd_register_uri_handler(server_, &ws) == ESP_OK;
    }

    bool broadcastJSON(const std::string& json) {
        std::lock_guard<std::mutex> lock(clients_mutex_);

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.payload = (uint8_t*)json.c_str();
        ws_pkt.len = json.length();
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        int sent = 0;
        for (auto& client : clients_) {
            esp_err_t ret = httpd_ws_send_frame_async(server_, client.fd, &ws_pkt);
            if (ret == ESP_OK) {
                sent++;
            } else {
                ESP_LOGW(TAG, "Failed to send to client fd=%d: %s",
                         client.fd, esp_err_to_name(ret));
            }
        }

        ESP_LOGD(TAG, "Broadcast to %d/%zu clients", sent, clients_.size());
        return sent > 0;
    }

    void onConnect(int fd) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        if (clients_.size() < MAX_CLIENTS) {
            clients_.push_back({fd, esp_timer_get_time()});
            ESP_LOGI(TAG, "Client connected: fd=%d, total=%zu", fd, clients_.size());
        } else {
            ESP_LOGW(TAG, "Max clients reached, rejecting fd=%d", fd);
        }
    }

    void onDisconnect(int fd) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::remove_if(clients_.begin(), clients_.end(),
                                 [fd](const Client& c) { return c.fd == fd; });
        clients_.erase(it, clients_.end());
        ESP_LOGI(TAG, "Client disconnected: fd=%d, remaining=%zu", fd, clients_.size());
    }
};

const char* WebSocketHandlerIDF::TAG = "WebSocketIDF";
```

#### 3.3 Migration Endpoints API REST

**Refactoriser:** `src/web_routes_api.cpp` pour utiliser `HttpServerIDF`

**Exemple: GET /api/status**
```cpp
// AVANT (AsyncWebServer)
server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    std::string json = buildStatusJSON();
    request->send(200, "application/json", json.c_str());
});

// APRÈS (esp_http_server)
httpServer.on("/api/status", HTTP_GET, [](httpd_req_t *req) {
    std::string json = buildStatusJSON();
    return HttpServerIDF::sendJSON(req, json);
});
```

**Endpoints à migrer (17 total):**
1. `GET /api/status`
2. `GET /api/settings`
3. `POST /api/settings`
4. `GET /api/logs`
5. `DELETE /api/logs`
6. `GET /api/diagnostics`
7. `GET /api/tinybms/registers`
8. `POST /api/tinybms/write`
9. `GET /api/cvl/state`
10. `GET /api/eventbus/stats`
11. `POST /api/wifi/scan`
12. `POST /api/wifi/connect`
13. `GET /api/system/info`
14. `POST /api/system/reboot`
15. `GET /` (index.html)
16. `GET /favicon.ico`
17. `WS /ws` (WebSocket)

#### 3.4 Tests de Charge WebSocket

**Créer:** `tests/stress/websocket_stress.py`
```python
import asyncio
import websockets
import json
import time

async def ws_client(client_id, uri, duration_s):
    """Client WebSocket qui se connecte et reçoit des messages"""
    received = 0
    latencies = []

    try:
        async with websockets.connect(uri) as ws:
            print(f"Client {client_id}: Connected")
            start = time.time()

            while time.time() - start < duration_s:
                msg = await ws.recv()
                recv_time = time.time()

                data = json.loads(msg)
                if 'timestamp_ms' in data:
                    latency = (recv_time * 1000) - data['timestamp_ms']
                    latencies.append(latency)

                received += 1

            print(f"Client {client_id}: Received {received} messages")
            print(f"Client {client_id}: Latency P50={latencies[len(latencies)//2]:.1f}ms, "
                  f"P95={latencies[int(len(latencies)*0.95)]:.1f}ms")

    except Exception as e:
        print(f"Client {client_id}: Error: {e}")

async def stress_test(num_clients, duration_s):
    """Stress test avec N clients simultanés"""
    uri = "ws://tinybms-bridge.local/ws"
    tasks = [ws_client(i, uri, duration_s) for i in range(num_clients)]
    await asyncio.gather(*tasks)

if __name__ == "__main__":
    # Test avec 4 clients pendant 5 minutes
    asyncio.run(stress_test(num_clients=4, duration_s=300))
```

**Exécuter:**
```bash
python tests/stress/websocket_stress.py
```

**Critères de succès:**
- Tous les clients reçoivent des messages (1 Hz)
- Latence P95 < 200ms
- Aucune déconnexion intempestive
- Heap stable (> 150 KB)

### ✅ Critères de Validation Phase 3

| # | Critère | Méthode de Validation | Seuil Acceptation |
|---|---------|----------------------|-------------------|
| 3.1 | **HTTP server démarre** | idf.py monitor | Log "HTTP server started" |
| 3.2 | **17 endpoints répondent** | Script curl | 200 OK pour tous |
| 3.3 | **WebSocket connect** | Test Python | 4 clients connectés |
| 3.4 | **WebSocket broadcast** | Test Python | 1 msg/s reçu |
| 3.5 | **Latence WebSocket P95** | Test Python | < 200ms |
| 3.6 | **JSON valide** | jq validation | Tous endpoints OK |
| 3.7 | **Stress 5min** | Test Python | Aucune erreur |
| 3.8 | **Heap après stress** | Monitor | > 150 KB |
| 3.9 | **CPU load** | GET /api/diagnostics | < 60% |
| 3.10 | **POST /api/settings** | Hot-reload test | Config appliquée |

### 🚨 Points d'Attention Phase 3

**Attention 1: WebSocket Latence**
- esp_http_server peut avoir latence > AsyncWebServer
- **Mitigation:**
  - Utiliser `httpd_ws_send_frame_async()` (non-bloquant)
  - Task dédiée pour broadcast (core 0)
  - Buffer size adapté (config.max_resp_headers)
- **Fallback:** Si latence > 300ms, revoir architecture (broadcast queue FreeRTOS)

**Attention 2: Mémoire**
- esp_http_server peut consommer plus de heap
- **Mitigation:**
  - Profiler avec `heap_caps_print_heap_info(MALLOC_CAP_8BIT)`
  - Réduire `max_uri_handlers` si nécessaire
  - Libérer buffers immédiatement après send
- **Fallback:** Si heap < 100 KB, limiter clients à 2

**Attention 3: Concurrence**
- esp_http_server handler peut bloquer autres requêtes
- **Mitigation:**
  - Handlers courts (< 50ms)
  - JSON pre-built (cache)
  - Mutex timeouts courts (50ms)
- **Fallback:** Si timeout fréquent, augmenter stack_size

### 🔄 Rollback Phase 3
**Condition:** Si critère 3.5 (latence) ou 3.7 (stress) échoue
**Action:**
1. Revenir à `ESPAsyncWebServer` temporairement
2. Investiguer root cause (profiling, logs)
3. Itération sur architecture WebSocket
4. Re-tester

**Note:** Rollback complexe car nécessite réintégrer Arduino libs

### 📊 Effort Estimé Phase 3
- **Durée:** 2-3 semaines
- **Charge:** 10-15 jours/homme
- **Ressources:** 1 développeur + 1 testeur
- **Parallélisation:** Non (critique)

---

## 📋 PHASE 4 : Optimisation & Finalisation (1 semaine)

### 🎯 Objectif
Optimiser pour ESP-IDF, nettoyer les dépendances Arduino résiduelles, finaliser documentation.

### 📦 Livrables

#### 4.1 Suppression Dépendances Arduino

**Supprimer:**
- `platformio.ini` (conserver pour référence historique)
- `#include <Arduino.h>` dans tous les fichiers
- Librairies Arduino: `ESPAsyncWebServer`, `AsyncTCP`, `sandeepmistry/CAN`

**Vérifier:**
```bash
# Aucune référence Arduino restante
grep -r "Arduino.h" src/
grep -r "AsyncWeb" src/
```

#### 4.2 Optimisations ESP-IDF

**4.2.1 Configuration sdkconfig**
```ini
# Performance
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y

# Mémoire
CONFIG_SPIRAM_SUPPORT=y  # Si ESP32-WROVER
CONFIG_SPIRAM_USE_MALLOC=y

# Security (optionnel)
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_FLASH_ENC_ENABLED=y

# Logging optimisé
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
CONFIG_LOG_COLORS=y
```

**4.2.2 Partition Table Optimisée**
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
spiffs,   data, spiffs,  ,        1M,
```

**4.2.3 Task Stack Tuning**
```cpp
// Mesurer high water mark
void printTaskStats() {
    UBaseType_t uartStack = uxTaskGetStackHighWaterMark(uartTaskHandle);
    UBaseType_t canStack = uxTaskGetStackHighWaterMark(canTaskHandle);
    UBaseType_t wsStack = uxTaskGetStackHighWaterMark(wsTaskHandle);

    ESP_LOGI("Tasks", "Stack HWM: UART=%u, CAN=%u, WS=%u",
             uartStack, canStack, wsStack);
}

// Ajuster stack sizes en conséquence
xTaskCreate(uartTask, "UART", 4096, NULL, 10, &uartTaskHandle);  // Au lieu de 8192
```

#### 4.3 Tests de Stabilité 24h

**Créer:** `tests/stability/longrun_test.sh`
```bash
#!/bin/bash
# Test de stabilité 24h

DEVICE_IP="tinybms-bridge.local"
DURATION_H=24
LOG_FILE="longrun_$(date +%Y%m%d_%H%M%S).log"

echo "Starting 24h stability test on $DEVICE_IP" | tee -a $LOG_FILE
START=$(date +%s)

while [ $(($(date +%s) - START)) -lt $((DURATION_H * 3600)) ]; do
    # Test API
    curl -s http://$DEVICE_IP/api/status > /dev/null
    if [ $? -ne 0 ]; then
        echo "$(date): API FAILED" | tee -a $LOG_FILE
    fi

    # Check diagnostics
    HEAP=$(curl -s http://$DEVICE_IP/api/diagnostics | jq '.heap_free')
    UPTIME=$(curl -s http://$DEVICE_IP/api/status | jq '.uptime_ms')

    echo "$(date): Heap=$HEAP KB, Uptime=$UPTIME ms" | tee -a $LOG_FILE

    # Check alarm si heap < 150KB
    if [ "$HEAP" -lt 150000 ]; then
        echo "$(date): WARNING: Low heap $HEAP KB" | tee -a $LOG_FILE
    fi

    sleep 60  # Check toutes les minutes
done

echo "24h test completed" | tee -a $LOG_FILE
```

**Exécuter:**
```bash
./tests/stability/longrun_test.sh
```

#### 4.4 Documentation Finale

**Mettre à jour:**
1. `README.md`: Nouvelle section "Build ESP-IDF"
2. `docs/ESP-IDF_MIGRATION_COMPLETE.md`: Rapport final
3. `docs/CHANGELOG.md`: v3.0.0 release notes
4. `docs/UPGRADE_GUIDE_v2_to_v3.md`: Guide utilisateur

**Créer:** `docs/ESP-IDF_BUILD.md`
```markdown
# TinyBMS - Build avec ESP-IDF

## Prérequis
- ESP-IDF v5.1+ (installé via `install.sh`)
- Python 3.8+
- Git

## Build
```bash
# Configurer
idf.py menuconfig

# Compiler
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration

Voir `sdkconfig.defaults` pour configuration par défaut.

Personnalisation:
```bash
idf.py menuconfig
# Component config → TinyBMS → ...
```
```

### ✅ Critères de Validation Phase 4

| # | Critère | Méthode de Validation | Seuil Acceptation |
|---|---------|----------------------|-------------------|
| 4.1 | **Aucune dép. Arduino** | grep -r "Arduino.h" | 0 résultats |
| 4.2 | **Build ESP-IDF pur** | idf.py build | Succès |
| 4.3 | **Flash size** | idf.py size | < 1 MB |
| 4.4 | **Heap > baseline** | Monitor boot | > 200 KB |
| 4.5 | **Test 24h stable** | Script longrun | Aucun crash |
| 4.6 | **Heap 24h stable** | Script longrun | > 150 KB constant |
| 4.7 | **Latences OK** | GET /api/diagnostics | UART<100ms, WS<200ms |
| 4.8 | **Tous tests passent** | idf.py test | 100% pass |
| 4.9 | **Docs à jour** | Review manuelle | Complètes |
| 4.10 | **Release notes** | CHANGELOG.md | v3.0.0 documentée |

### 🔄 Rollback Phase 4
**Condition:** Si test 24h échoue (crash, heap leak, watchdog)
**Action:**
1. Analyser coredump (`idf.py coredump-info`)
2. Identifier root cause (leak, stack overflow, deadlock)
3. Corriger et re-itérer
4. Ne pas release v3.0.0 tant que instable

### 📊 Effort Estimé Phase 4
- **Durée:** 1 semaine
- **Charge:** 5 jours/homme
- **Ressources:** 1 développeur
- **Parallélisation:** Non

---

## 📊 Récapitulatif Global

### Timeline Total

```
Semaine 1-2  │████████░░░░░░░░░░░░│ Phase 1: Fondations ESP-IDF
Semaine 3-4  │░░░░░░░░████████░░░░│ Phase 2: Migration Périphériques
Semaine 5-7  │░░░░░░░░░░░░████████│ Phase 3: Migration WebServer
Semaine 8    │░░░░░░░░░░░░░░░░████│ Phase 4: Optimisation
             └──────────────────────
              4-6 semaines total
```

### Effort Total
- **Durée:** 4-6 semaines
- **Charge:** 25-40 jours/homme
- **FTE:** 1 développeur temps plein

### Budget Risque par Phase

| Phase | Risque | Probabilité | Impact | Mitigation |
|-------|--------|-------------|--------|-----------|
| Phase 1 | Build échoue | Faible | Faible | Tests CI, docs ESP-IDF |
| Phase 2 | Latence UART | Moyen | Moyen | Profiling, rollback HAL |
| Phase 3 | WebSocket latence | **Élevé** | **Élevé** | Tests stress, fallback AsyncWS |
| Phase 4 | Instabilité 24h | Moyen | Élevé | Coredump, watchdog |

### Métriques Finales Cibles

| Métrique | v2.5.0 (Actuel) | v3.0.0 (Cible) | Tolérance |
|----------|-----------------|----------------|-----------|
| **Latence UART** | 70-80ms | < 100ms | ±30% |
| **Latence WebSocket** | 80-120ms | < 200ms | ±60% |
| **Heap libre** | 180-220 KB | > 200 KB | +10% |
| **Flash size** | ~500 KB | < 1 MB | +100% |
| **Uptime MTBF** | > 24h | > 24h | Identique |
| **CPU load** | 15-25% | < 30% | +20% |

---

## ✅ Critères de Go/No-Go Globaux

### Go Phase 1 → Phase 2
- ✅ Build ESP-IDF réussit
- ✅ Tous HAL IDF testés (unit tests)
- ✅ Build Arduino inchangé (coexistence)

### Go Phase 2 → Phase 3
- ✅ UART polling fonctionne (HAL IDF)
- ✅ CAN TX/RX fonctionne (HAL IDF)
- ✅ Latences dans seuils (<150ms)
- ✅ Heap stable (>150KB)

### Go Phase 3 → Phase 4
- ✅ HTTP server répond (17 endpoints)
- ✅ WebSocket stable (4 clients)
- ✅ Latence WebSocket acceptable (<200ms)
- ✅ Stress 5min OK (aucune erreur)

### Go Phase 4 → Release v3.0.0
- ✅ Aucune dépendance Arduino
- ✅ Test 24h stable (aucun crash)
- ✅ Heap stable 24h (>150KB)
- ✅ Toutes métriques dans seuils
- ✅ Documentation complète

---

## 📚 Documentation de Compatibilité Ascendante

### Pour l'Utilisateur Final

**✅ Aucun changement requis:**
1. **Configuration JSON** (`/spiffs/config.json`) identique
2. **API REST** endpoints identiques (URL, paramètres, réponses)
3. **WebSocket** protocole identique (ws://tinybms-bridge.local/ws)
4. **Protocole CAN** Victron inchangé
5. **Protocole UART** TinyBMS inchangé
6. **mDNS hostname** identique (tinybms-bridge.local)

**⚠️ Changements internes (transparents):**
1. **Build system** PlatformIO → ESP-IDF (utilisateur ne voit pas)
2. **Logging format** Serial → esp_log (timestamps différents)
3. **Taille binaire** ~500KB → ~800KB (ne concerne pas l'utilisateur)

### Pour le Développeur

**✅ APIs stables:**
1. **HAL interfaces** (`IHalUart`, `IHalCan`, etc.) inchangées
2. **EventBus API** inchangée
3. **ConfigManager API** inchangée
4. **Logger API** inchangée (backend change)

**⚠️ APIs modifiées:**
1. **WebServer setup** (`web_server_setup.cpp`) → Nouveau wrapper
2. **Build commandes** `pio run` → `idf.py build`
3. **Flash commandes** `pio run -t upload` → `idf.py flash`

---

## 🔧 Plan de Rollback Global

### Scénario 1: Échec Phase 1
**Impact:** Aucun (coexistence)
**Action:** Supprimer fichiers ESP-IDF, continuer PlatformIO
**Durée:** 1h

### Scénario 2: Échec Phase 2
**Impact:** Périphériques non fonctionnels
**Action:** Revenir `ESP32Factory` (Arduino HAL)
**Durée:** 30min

### Scénario 3: Échec Phase 3 (Critique)
**Impact:** Pas d'interface web
**Action:**
1. Revenir ESPAsyncWebServer (réintégrer lib Arduino)
2. Investiguer root cause latence
3. Itérer sur architecture
**Durée:** 1-2 jours

### Scénario 4: Échec Phase 4
**Impact:** Instabilité production
**Action:**
1. Ne pas release v3.0.0
2. Analyser coredump
3. Corriger et re-tester 24h
**Durée:** 2-5 jours

---

## 📞 Support et Contact

**Questions sur ce plan:**
- GitHub Issues: `migration-esp-idf` label
- Email: [voir profil GitHub]

**Références:**
- ESP-IDF Programming Guide: https://docs.espressif.com/projects/esp-idf/
- TinyBMS Docs: `/docs/`
- Victron CAN Spec: `/docs/victron_register_mapping.md`

---

**Dernière mise à jour:** 2025-10-30
**Auteur:** Claude (Migration Planning Assistant)
**Version:** 1.0 (Proposition initiale)
