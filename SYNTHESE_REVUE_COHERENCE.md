# Synthèse de la Revue de Cohérence du Projet TinyBMS

**Date:** 2025-10-29
**Version:** 2.5.0
**Score Global:** 7.5/10

---

## 🎯 Résumé Exécutif

Le projet présente une **architecture Event Bus solide** mais souffre de **3 race conditions critiques** sur les structures partagées nécessitant une action immédiate.

---

## ✅ Points Forts (9/10 modules fonctionnels)

1. **Architecture découplée** - Event Bus centralisé performant
2. **Documentation exhaustive** - README par module, diagrammes UML
3. **Tests robustes** - Intégration Python, tests natifs CVL, stubs UART
4. **Initialisation correcte** - Séquence mutex → config → Event Bus → tâches
5. **API Web complète** - REST + WebSocket avec fallback gracieux

---

## ⚠️ Problèmes Critiques (Action Immédiate)

### 🔴 CRITIQUE #1: Race Condition `bridge.live_data_`
**Fichiers:** `bridge_uart.cpp:277`, `bridge_can.cpp:72,102,344,353...`

```cpp
// UART Task (Writer)
bridge->live_data_ = d;  // 880 bytes NON-PROTÉGÉ

// CAN Task (Reader) - 9+ localisations
VictronMappingContext ctx{bridge.live_data_, ...};  // Lecture directe
```

**Impact:** Corruption données PGN Victron sous charge
**Solution:** Créer `liveMutex` ou utiliser UNIQUEMENT Event Bus cache

---

### 🔴 CRITIQUE #2: Race Condition `bridge.stats`
**Fichiers:** `bridge_uart.cpp:145`, `bridge_can.cpp`, `bridge_cvl.cpp:138`, `json_builders.cpp:113`

```cpp
// 3 tâches écrivent SANS MUTEX:
bridge.stats.uart_success_count++;  // UART
bridge.stats.can_tx_count++;         // CAN
bridge.stats.cvl_current_v = ...;    // CVL

// JSON API lit SANS MUTEX:
doc["stats"]["can_tx_count"] = bridge.stats.can_tx_count;
```

**Impact:** Compteurs corrompus, stats incohérentes
**Solution:** Créer `statsMutex` pour protéger toutes lectures/écritures

---

### 🔴 CRITIQUE #3: Double Source de Vérité
**Fichiers:** `bridge_uart.cpp:277-278`, `bridge_can.cpp:646`

```cpp
// UART publie dans 2 sources:
bridge->live_data_ = d;              // Source #1
eventBus.publishLiveData(d);         // Source #2

// CAN lit des 2 sources:
if (eventBus.getLatestLiveData(d)) bridge->live_data_ = d;  // Update
VictronMappingContext ctx{bridge.live_data_, ...};          // Accès direct
```

**Impact:** Désynchronisation Event Bus ↔ bridge.live_data_
**Solution:** Choisir UNE source (recommandé: Event Bus seul)

---

## 🟡 Problèmes Haute Priorité

### 4. Timeout configMutex Incohérent
- `bridge_uart.cpp:67` utilise **25ms** (vs 100ms partout ailleurs)
- Risque fallback silencieux sous charge
- **Solution:** Uniformiser à 100ms minimum

### 5. Config Thresholds Sans Mutex
- `bridge_uart.cpp:280` lit `config.victron.thresholds` sans `configMutex`
- Décisions alarmes basées sur thresholds qui peuvent changer
- **Solution:** Protéger avec configMutex

---

## 🟢 Problèmes Moyenne Priorité

6. **SPIFFS redondant** - ConfigManager ET Logger appellent `SPIFFS.begin()`
7. **Ordre publication** - Registres MQTT publiés AVANT live_data (incohérence temporaire)
8. **Stats JSON non-protégées** - Lectures multiples de bridge.stats sans mutex

---

## 🎯 Plan d'Actions Correctives

### Phase 1 - CRITIQUE (Semaine 1) - ~10h

```cpp
// 1. Créer liveMutex
SemaphoreHandle_t liveMutex = xSemaphoreCreateMutex();

// UART Task
xSemaphoreTake(liveMutex, pdMS_TO_TICKS(50));
bridge->live_data_ = d;
xSemaphoreGive(liveMutex);

// CAN Task
xSemaphoreTake(liveMutex, pdMS_TO_TICKS(50));
VictronMappingContext ctx{bridge.live_data_, bridge.stats};
xSemaphoreGive(liveMutex);

// 2. Créer statsMutex
SemaphoreHandle_t statsMutex = xSemaphoreCreateMutex();

// Toutes les tâches
xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10));
bridge.stats.can_tx_count++;
xSemaphoreGive(statsMutex);

// 3. OU mieux: Éliminer bridge.live_data_, utiliser UNIQUEMENT Event Bus
// Supprimer: bridge.live_data_ = d;
// Remplacer partout par: TinyBMS_LiveData local; eventBus.getLatestLiveData(local);
```

### Phase 2 - HAUTE (Semaine 2) - ~5h

```cpp
// 4. Uniformiser timeout configMutex
xSemaphoreTake(configMutex, pdMS_TO_TICKS(100));  // Partout (pas 25ms)

// 5. Protéger thresholds
xSemaphoreTake(configMutex, pdMS_TO_TICKS(100));
const auto& th = config.victron.thresholds;
xSemaphoreGive(configMutex);
```

### Phase 3 - MÉDIA (Semaine 3) - ~4.5h

- Mutualiser SPIFFS.begin() dans system_init uniquement
- Inverser ordre publication (live_data puis registres MQTT)
- Tests WebSocket stress réseau

---

## 📊 Scores par Module

| Module | Score | Problèmes |
|--------|-------|-----------|
| System Init | 9/10 | - |
| Event Bus | 8/10 | Ordre publication |
| Config Manager | 8/10 | Timeout 25ms, SPIFFS dup |
| **UART TinyBMS** | **6/10** | **live_data_ non-protégé** |
| **Bridge CAN** | **6/10** | **live_data_ + stats** |
| CVL Algorithm | 8/10 | stats non-protégé |
| Watchdog | 9/10 | - |
| Logger | 7/10 | SPIFFS redondant |
| Web/API/JSON | 8/10 | stats non-protégé |
| Keep-Alive | 9/10 | - |
| WebSocket | 8/10 | Tests incomplets |
| MQTT Bridge | 8/10 | - |

**Moyenne:** 7.8/10

---

## 🚀 Impact Estimé

**Après corrections Phase 1-2:**
Score passe de **7.5/10 à 9.5/10**

- ✅ Élimination race conditions critiques
- ✅ Architecture cohérente (Event Bus seul)
- ✅ Fiabilité production garantie
- ✅ Maintenance simplifiée

---

## 📄 Rapport Détaillé

Un rapport complet de 1237 lignes avec analyse fichier par fichier, matrices d'interopérabilité et diagrammes de flux est disponible:

**Fichier:** `docs/RAPPORT_COHERENCE_COMPLETE.md`
**Branche:** `claude/project-coherence-review-011CUbNkTpmTAVX28hi6Bu1a`

Pour le consulter:
```bash
git checkout claude/project-coherence-review-011CUbNkTpmTAVX28hi6Bu1a
cat docs/RAPPORT_COHERENCE_COMPLETE.md
```

---

## 📋 Checklist Actions Immédiates

- [ ] **Créer liveMutex** (protection bridge.live_data_)
- [ ] **Créer statsMutex** (protection bridge.stats)
- [ ] **OU Éliminer bridge.live_data_** (Event Bus seul - recommandé)
- [ ] **Uniformiser timeout configMutex** (100ms minimum)
- [ ] **Protéger config.victron.thresholds** (avec configMutex)
- [ ] **Tests validation** (race conditions corrigées)

**Priorité:** CRITIQUE
**Effort total:** ~15h (Phase 1+2)
**Bénéfice:** Fiabilité production + architecture cohérente

---

**Revue générée par:** Claude Code Agent
**Contact:** Voir rapport détaillé pour références code précises
