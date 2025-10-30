# Migration ESP-IDF pour TinyBMS-Victron Bridge

## Objectifs
- Utiliser ESP-IDF comme runtime principal pour profiter des drivers officiels (TWAI, Wi-Fi, NVS, watchdog)
- Conserver la couche applicative Arduino existante pour accélérer la transition
- Documenter les fichiers impactés et les points de vigilance lors de la compilation sous ESP-IDF

## Configuration PlatformIO
- `platformio.ini` utilise désormais `framework = arduino, espidf`
- Dépendance `sandeepmistry/CAN` supprimée (driver TWAI natif via ESP-IDF)

## Fichiers impactés
- `src/hal/esp32/esp32_can.cpp`: driver CAN basé sur `driver/twai.h`
- `src/hal/esp32/esp32_gpio.cpp`: GPIO pilotés via API Arduino (composant) et compatibles ESP-IDF
- `src/hal/esp32/esp32_uart.cpp`: UART TinyBMS utilisant `HardwareSerial` (Arduino component)
- `src/hal/esp32/esp32_watchdog.cpp`: watchdog matériel via `esp_task_wdt`
- `src/hal/esp32/esp32_storage.cpp`: montage SPIFFS/NVS
- `src/mqtt/victron_mqtt_bridge.cpp`: client MQTT natif ESP-IDF
- `src/watchdog_manager.cpp`: coordination du watchdog système

## Checklist de compilation
1. Installer l'outil ESP-IDF via PlatformIO (`platformio platform install espressif32`)
2. Lancer `pio run` pour vérifier la compilation croisée FreeRTOS + Arduino component
3. Déployer via `pio run -t upload` et vérifier la trace série 115200 bauds

## Étapes suivantes
- Migrer progressivement les modules dépendants d'`Arduino.h` vers des abstractions HAL pures
- Ajouter des tests natifs ciblant les implémentations ESP-IDF
- Mettre à jour la documentation module par module lorsque le portage complet sera achevé
