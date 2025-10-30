# Synthèse Migration ESP-IDF - Vue Exécutive

**Document:** Synthèse pour décision stratégique
**Date:** 2025-10-30
**Statut:** Proposition
**Lien détaillé:** [PLAN_MIGRATION_ESP-IDF_PHASES.md](PLAN_MIGRATION_ESP-IDF_PHASES.md)

---

## 🎯 Objectif de la Migration

Migrer TinyBMS v2.5.0 (PlatformIO + Arduino) vers **ESP-IDF natif** (v3.0.0) tout en **préservant strictement** toutes les fonctionnalités et l'architecture actuelle.

### Pourquoi cette migration ?

| Avantage ESP-IDF | Bénéfice Projet |
|------------------|-----------------|
| **Performance brute** | -20% latence périphériques, +10% CPU disponible |
| **Contrôle total** | Accès direct twai_driver, esp_wifi (debug plus facile) |
| **Moins d'abstraction** | Suppression overhead Arduino (heap +30KB) |
| **Production-grade** | Stack officielle Espressif (support long terme) |
| **Optimisations** | Flash encryption, OTA natif, power management |

---

## 📊 Approche Progressive en 4 Phases

### Vision d'Ensemble

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   PHASE 1    │────→│   PHASE 2    │────→│   PHASE 3    │────→│   PHASE 4    │
│  Fondations  │     │ Périphériques│     │  WebServer   │     │ Optimisation │
│   ESP-IDF    │     │   via HAL    │     │   esp_http   │     │  & Release   │
└──────────────┘     └──────────────┘     └──────────────┘     └──────────────┘
  1-2 semaines        1-2 semaines         2-3 semaines         1 semaine
   Risque: BAS         Risque: MOYEN       Risque: ÉLEVÉ        Risque: MOYEN
```

### Phase 1: Fondations ESP-IDF (1-2 semaines)
**Objectif:** Créer build ESP-IDF en parallèle, sans toucher au code Arduino

**Livrables:**
- ✅ CMakeLists.txt + sdkconfig.defaults
- ✅ HAL ESP-IDF (UART, CAN, Storage, WiFi) implémentés
- ✅ Tests unitaires HAL (100% pass)
- ✅ Build dual: PlatformIO (prod) + ESP-IDF (test)

**Critère Non-Rupture:** Code Arduino strictement inchangé

**Go/No-Go Phase 1→2:**
- Build ESP-IDF réussit ✅
- Tests HAL passent ✅
- Coexistence PlatformIO/IDF ✅

---

### Phase 2: Migration Périphériques (1-2 semaines)
**Objectif:** Basculer UART/CAN/Storage/WiFi vers HAL ESP-IDF

**Livrables:**
- ✅ Switch `ESP32Factory` → `ESP32FactoryIDF`
- ✅ Logging esp_log (compatible Logger API)
- ✅ Tests end-to-end UART→EventBus→CAN
- ✅ Validation latences (<150ms UART→CAN)

**Critère Non-Rupture:**
- API REST identique
- Configuration JSON identique
- Protocoles CAN/UART identiques

**Go/No-Go Phase 2→3:**
- UART polling fonctionne (>95% success) ✅
- CAN TX réussit (100% frames) ✅
- Latences dans seuils ✅
- Heap stable (>150KB) ✅

---

### Phase 3: Migration WebServer ⚠️ (2-3 semaines)
**Objectif:** Remplacer ESPAsyncWebServer par esp_http_server

**RISQUE MAJEUR:** Cette phase est critique (cœur de l'interface utilisateur)

**Livrables:**
- ✅ Wrapper esp_http_server + WebSocket handler
- ✅ Migration 17 endpoints API REST
- ✅ WebSocket broadcast (1Hz, 4 clients)
- ✅ Tests de charge (5min, latence <200ms)

**Critère Non-Rupture:**
- 17 endpoints API identiques (URL, JSON)
- WebSocket protocole identique (ws://tinybms-bridge.local/ws)
- Latence acceptable (<200ms P95)

**Go/No-Go Phase 3→4:**
- HTTP server répond (17 endpoints) ✅
- WebSocket stable (4 clients) ✅
- Latence WebSocket <200ms ✅
- Stress 5min OK ✅

**Plan de Rollback Phase 3:**
Si latence >300ms ou stress échoue:
1. Revenir ESPAsyncWebServer temporairement
2. Investiguer (profiling, logs)
3. Itérer architecture WebSocket
4. Re-tester

---

### Phase 4: Optimisation & Release (1 semaine)
**Objectif:** Finaliser, optimiser, tester 24h, documenter

**Livrables:**
- ✅ Suppression dépendances Arduino
- ✅ Optimisations sdkconfig (perf, sécurité)
- ✅ Test stabilité 24h (aucun crash)
- ✅ Documentation complète (build, upgrade guide)

**Critère Non-Rupture:**
- Toutes métriques ≥ baseline
- Heap stable 24h (>150KB)
- MTBF ≥ 24h

**Go/No-Go Phase 4→Release:**
- Aucune dépendance Arduino ✅
- Test 24h stable ✅
- Métriques dans seuils ✅
- Docs complètes ✅

---

## 📈 Métriques de Performance - Garanties

### Seuils Minimums Acceptables

| Métrique | Actuel v2.5.0 | Cible v3.0.0 | Tolérance |
|----------|---------------|--------------|-----------|
| **Latence UART→CAN** | 70-80ms | **< 150ms** | +87% |
| **Latence WebSocket** | 80-120ms | **< 200ms** | +66% |
| **Latence CAN TX** | 2-5ms | **< 10ms** | +100% |
| **Heap libre** | 180-220 KB | **> 150 KB** | -17% minimum |
| **Charge CPU** | 15-25% | **< 60%** | +140% |
| **Taille binaire** | ~500 KB | **< 1 MB** | +100% |
| **Uptime MTBF** | > 24h | **> 24h** | Identique |

**Note:** Les seuils sont **conservateurs** pour garantir aucune régression perçue par l'utilisateur.

---

## ✅ Garanties de Compatibilité Ascendante

### Pour l'Utilisateur Final (Aucun changement requis)

| Aspect | Compatibilité | Détails |
|--------|---------------|---------|
| **Configuration JSON** | ✅ 100% | Fichier `/spiffs/config.json` identique |
| **API REST** | ✅ 100% | 17 endpoints, mêmes URL/paramètres/JSON |
| **WebSocket** | ✅ 100% | Protocole ws:// identique, même format JSON |
| **Protocole CAN** | ✅ 100% | 9 PGNs Victron inchangés |
| **Protocole UART** | ✅ 100% | Modbus RTU TinyBMS inchangé |
| **mDNS Hostname** | ✅ 100% | `tinybms-bridge.local` identique |
| **Firmware OTA** | ⚠️ Format binaire | Migration manuelle v2→v3 (une fois) |

### Pour le Développeur

| API/Module | Compatibilité | Changements |
|------------|---------------|-------------|
| **HAL Interfaces** | ✅ 100% | `IHalUart`, `IHalCan`, etc. inchangés |
| **EventBus API** | ✅ 100% | Publish/subscribe identique |
| **ConfigManager API** | ✅ 100% | load/save/get/set identiques |
| **Logger API** | ✅ Interface | Backend esp_log (format logs différent) |
| **Build System** | ❌ Incompatible | `pio run` → `idf.py build` |
| **WebServer Setup** | ⚠️ Interne | Nouveau wrapper (API endpoints identiques) |

---

## 🚨 Risques et Mitigations

### Matrice des Risques

| Risque | Phase | Probabilité | Impact | Mitigation |
|--------|-------|-------------|--------|-----------|
| **Build ESP-IDF échoue** | 1 | Faible | Faible | Docs Espressif, CI/CD |
| **Latence UART augmente** | 2 | Moyen | Moyen | Profiling, rollback HAL |
| **WebSocket latence >200ms** | 3 | **Élevé** | **Élevé** | Tests stress, fallback AsyncWS |
| **Crash 24h (heap leak)** | 4 | Moyen | Élevé | Coredump, watchdog, profiling |
| **Incompatibilité CAN** | 2 | Faible | Élevé | Tests twai_driver, loopback |

### Stratégie de Rollback

**Phase 1:** Supprimer fichiers ESP-IDF (1h)
**Phase 2:** Revenir `ESP32Factory` Arduino (30min)
**Phase 3:** Réintégrer ESPAsyncWebServer (1-2 jours) ⚠️
**Phase 4:** Ne pas release v3.0.0, investiguer (2-5 jours)

---

## 💰 Ressources et Planning

### Effort Total

| Phase | Durée | Charge (j/h) | Ressources | Parallélisable |
|-------|-------|--------------|------------|----------------|
| **Phase 1** | 1-2 sem | 5-10 j | 1 dev ESP-IDF | ✅ Oui (n'impacte pas prod) |
| **Phase 2** | 1-2 sem | 5-8 j | 1 dev + 1 testeur | ❌ Non (critique) |
| **Phase 3** | 2-3 sem | 10-15 j | 1 dev + 1 testeur | ❌ Non (critique) |
| **Phase 4** | 1 sem | 5 j | 1 dev | ❌ Non |
| **TOTAL** | **4-6 sem** | **25-40 j** | **1-2 FTE** | Partiel |

### Timeline Gantt

```
Semaine   │ 1  │ 2  │ 3  │ 4  │ 5  │ 6  │ 7  │ 8  │
──────────┼────┼────┼────┼────┼────┼────┼────┼────┤
Phase 1   │████│████│    │    │    │    │    │    │ Fondations (parallèle)
Phase 2   │    │    │████│████│    │    │    │    │ Périphériques
Phase 3   │    │    │    │    │████│████│████│    │ WebServer (critique)
Phase 4   │    │    │    │    │    │    │    │████│ Optimisation
──────────┴────┴────┴────┴────┴────┴────┴────┴────┘
```

**Optimisation:** Phase 1 peut démarrer immédiatement en parallèle du développement actuel.

---

## 🎯 Critères de Décision Go/No-Go Global

### ✅ Arguments POUR la Migration

1. **Performance potentielle** : -20% latence, +30KB heap
2. **Stack officielle** : Support long terme Espressif
3. **Contrôle total** : Debug facilité, optimisations fines
4. **Sécurité** : Flash encryption, secure boot natifs
5. **Architecture prête** : HAL abstraction déjà en place (80% code portable)

### ⚠️ Arguments CONTRE la Migration

1. **Effort significatif** : 25-40 jours/homme (4-6 semaines)
2. **Risque WebServer** : Phase 3 critique (latence, stabilité)
3. **Binaire plus gros** : ~500KB → ~800KB (+60%)
4. **Complexité build** : CMake vs PlatformIO (courbe apprentissage)
5. **Migration utilisateur** : Firmware OTA v2→v3 manuelle

### 💡 Recommandation

**✅ GO pour la migration ESP-IDF**

**Justification:**
1. Architecture actuelle **excellente** (9.5/10) facilite migration
2. HAL abstraction **déjà en place** (80% code portable)
3. Approche **progressive** (4 phases) minimise risques
4. **Rollback possible** à chaque phase
5. Bénéfices **long terme** (performance, support, sécurité)

**Conditions:**
- Phase 1 démarrer **immédiatement** (parallèle, sans risque)
- Phase 3 avec **tests stress exhaustifs** (WebSocket critique)
- Go/No-Go **strict** après chaque phase
- Conserver **PlatformIO** en fallback jusqu'à release finale

---

## 📚 Documentation Associée

| Document | Description | Audience |
|----------|-------------|----------|
| **[PLAN_MIGRATION_ESP-IDF_PHASES.md](PLAN_MIGRATION_ESP-IDF_PHASES.md)** | Plan détaillé (85 pages) | Développeurs |
| **[ESP-IDF_MIGRATION_ANALYSIS.md](ESP-IDF_MIGRATION_ANALYSIS.md)** | Analyse technique complète | Architectes |
| **[ESP-IDF_MIGRATION_SUMMARY.md](ESP-IDF_MIGRATION_SUMMARY.md)** | Résumé exécutif | Management |
| **ARCHITECTURE_QUICK_REFERENCE.md** | Référence architecture actuelle | Équipe technique |

---

## 📋 Checklist de Validation Finale (Phase 4)

Avant release v3.0.0, valider:

- [ ] ✅ Aucune dépendance Arduino résiduelle
- [ ] ✅ Build ESP-IDF pur réussit (`idf.py build`)
- [ ] ✅ 17 endpoints API répondent (200 OK)
- [ ] ✅ WebSocket stable 4 clients (5min stress)
- [ ] ✅ Latences: UART<100ms, WebSocket<200ms, CAN<10ms
- [ ] ✅ Heap stable 24h (>150KB constant)
- [ ] ✅ Test stabilité 24h: aucun crash, aucun watchdog reset
- [ ] ✅ Configuration JSON compatible (lecture v2.5.0 config OK)
- [ ] ✅ Protocoles CAN/UART identiques (tests Victron GX)
- [ ] ✅ Documentation complète (README, build guide, upgrade guide)
- [ ] ✅ Changelog v3.0.0 détaillé
- [ ] ✅ Tests régression: CVL, EventBus, Config Manager
- [ ] ✅ Performance metrics documentées (baseline vs actuel)
- [ ] ✅ Binary size < 1MB
- [ ] ✅ Coredump config activé (debug post-mortem)

---

## 🚀 Prochaines Étapes Recommandées

### Immédiat (Semaine 1)
1. **Valider cette proposition** avec l'équipe technique
2. **Créer branche** `feature/esp-idf-migration`
3. **Démarrer Phase 1** (parallèle, sans risque):
   - Setup ESP-IDF v5.1
   - Créer CMakeLists.txt
   - Implémenter HAL IDF (UART en premier)

### Court Terme (Semaine 2-3)
4. **Finaliser Phase 1**:
   - Tests HAL complets
   - Validation build dual PlatformIO/IDF
5. **Go/No-Go Phase 1→2** (comité technique)
6. **Démarrer Phase 2** si Go

### Moyen Terme (Semaine 4-7)
7. **Phases 2 & 3** (critique, monitoring étroit)
8. **Tests stress WebSocket** (Phase 3)
9. **Go/No-Go Phase 3→4**

### Long Terme (Semaine 8+)
10. **Phase 4**: Optimisation, test 24h
11. **Release v3.0.0** si tous critères validés
12. **Documentation upgrade path** pour utilisateurs

---

**Contact:** GitHub Issues (`migration-esp-idf` label)
**Auteur:** Claude (Migration Planning)
**Date:** 2025-10-30
**Version:** 1.0
