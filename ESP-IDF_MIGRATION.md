# Migration ESP-IDF – État final

La migration vers ESP-IDF est **terminée** et l’ensemble des fonctionnalités historiques est désormais assuré par des composants natifs : TWAI, UART, Wi-Fi, SPIFFS, HTTP serveur et stockage NVS.

## Résumé

- ✅ Remplacement complet du code Arduino (`.ino`, AsyncWebServer, WiFi Arduino, SPIFFS Arduino…)
- ✅ Nouvelle architecture `app_main` ESP-IDF avec CMake et `main/`
- ✅ Tâches FreeRTOS explicites (UART, CAN, diagnostics)
- ✅ Configuration via Kconfig (`menuconfig`) pour la couche matérielle
- ✅ Wi-Fi AP/STA et API REST implémentés avec `esp_wifi` et `esp_http_server`
- ✅ Interface web historique servie depuis SPIFFS (`esp_vfs_spiffs`) + WebSocket natif pour la télémétrie
- ✅ Journalisation ESP-IDF capturée et exposée via `/api/logs/*` comme dans la version Arduino
- ✅ Conservation de l’ancien code sous `legacy/` pour référence

## Checklist de build

```bash
idf.py set-target esp32
idf.py menuconfig   # régler pins et cadence
idf.py build
idf.py flash monitor
idf.py spiffs
idf.py -p /dev/ttyUSB0 spiffs-flash
```

## Suivi

- [x] Mise à jour `platformio.ini` (`framework = espidf` uniquement)
- [x] Suppression des bibliothèques Arduino (ESPAsyncWebServer, ArduinoJson, etc.)
- [x] Nouvelle documentation (README) décrivant l’architecture ESP-IDF
- [x] Portage de la configuration/web UI via `esp_http_server`, SPIFFS et NVS
- [ ] Ajouter des tests unitaires `unity` spécifiques au parsing UART et à l’encodage CAN (prochain jalon)
- [ ] Couvrir les API REST/WebSocket par des tests d’intégration automatisés

La base de code est prête pour des évolutions 100 % ESP-IDF (tests automatisés, MQTT natif, WebSocket temps réel, etc.).
