# Migration ESP-IDF – État final

La migration vers ESP-IDF est **terminée**. Le firmware TinyBMS ↔ Victron fonctionne désormais exclusivement avec les APIs natives (TWAI, UART, FreeRTOS, esp_timer) sans aucune dépendance au runtime Arduino.

## Résumé

- ✅ Remplacement complet du code Arduino (`.ino`, AsyncWebServer, WiFi Arduino, SPIFFS Arduino…)
- ✅ Nouvelle architecture `app_main` ESP-IDF avec CMake et `main/`
- ✅ Tâches FreeRTOS explicites (UART, CAN, diagnostics)
- ✅ Configuration via Kconfig (`menuconfig`)
- ✅ CAN / UART gérés avec les drivers `driver/twai.h` et `driver/uart.h`
- ✅ Conservation de l’ancien code sous `legacy/` pour référence

## Checklist de build

```bash
idf.py set-target esp32
idf.py menuconfig   # régler pins et cadence
idf.py build
idf.py flash monitor
```

## Suivi

- [x] Mise à jour `platformio.ini` (`framework = espidf` uniquement)
- [x] Suppression des bibliothèques Arduino (ESPAsyncWebServer, ArduinoJson, etc.)
- [x] Nouvelle documentation (README) décrivant l’architecture ESP-IDF
- [ ] Ajouter des tests unitaires `unity` spécifiques au parsing UART et à l’encodage CAN (prochain jalon)

La base de code est prête pour des évolutions 100 % ESP-IDF (tests, MQTT natif, Web UI basée sur `esp_http_server`, etc.).
